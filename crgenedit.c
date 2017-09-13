//NOTE: this is made by Christopher Kello. I made small edits to make its output work better with my visualization 
//but the vast majority of this code is attributed to him.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#define RANDPROB (((double)rand()+1.0) / ((double)RAND_MAX+2.0))
#define INTBIT 1 	// the bit was/is routed from a location internal to the block
#define EXTBIT 2	// the bit was/is routed from a location external to the block
#define MINDELAY 0.000001	// to avoid instanteous delays, and route internal bits to arrive just before externals

typedef struct {
	char rr, s3;			// rerouting flag, and 3 possible states of the arc:
							// bit not present (0), bit present (1), or bit present from outside block (2)
	double t, d;			// arrival time of bit, route delay from D or external environment
	unsigned int i, j, k; 	// location of trigger, origin, and destination/current location
	void *next; 			// linked list for iterating through locations with bits
} arcType;

typedef struct {
	char id, **C, learn, print;	// ID number for block, binary coupling matrix, learn flag, print flag
	unsigned int n, nC, **R;	// number of bit locations, couplings between locations (including self-couplings), 
								// and routing matrix R[i][j]: triggered at i projecting from j
	arcType *A, *As3;		// array of routing info for bit locations, and pointer to list of arcs with s3 > 0
	double **D, **LT;		// delay matrix, matrix of last rerouting times
	void *Blink;			// pointer to another block where bits ejected by spikes go to 
							// (if NULL, these bits disappear)
} blockType;

typedef struct {
	arcType *a;				// arc info in transit
	void *next, *prev;		// linked list pointers
	blockType *blk;			// block where arc info is going
} eventType;

typedef struct {
	unsigned int n;					// number of events in queue, and links to front and back of queue
	eventType *nearest, *farthest;	// front and back of linked list
} queueType;


double t = 0.0, timescale, *delay = NULL; 
unsigned int nump = 0, blen = 0, numExt0 = 0, dd = 0, sumrr = 0;
size_t vsize = sizeof( eventType );
queueType q; blockType *Barray = NULL;
char toggle = 0;

//////////////////////////////////////////////////////////

void *allocMem( size_t size ) {
	void *p;
	if( (p = calloc( 1, size )) == NULL )
		{ printf( "\nbad calloc\n" ); exit(0); }
	return p;
}

void addBlocks( unsigned int n ) {
	blockType *blk;
	unsigned int i, j, k;

	Barray = allocMem( sizeof( blockType ) * n );
	for( k = 0; k < n; k++ ) { blk = &Barray[k];
		blk->id = k; blk->n = blen; blk->learn = 1; blk->print = 1; 
		blk->A = allocMem( sizeof( arcType ) * blen );
		blk->R = allocMem( sizeof( unsigned int * ) * blen );
		blk->C = allocMem( sizeof( char * ) * blen );
		blk->D = allocMem( sizeof( double * ) * blen );
		blk->LT = allocMem( sizeof( double * ) * blen );
		for( i = 0; i < blen; i++ ) {
			blk->R[i] = allocMem( sizeof( unsigned int ) * blen );
			blk->C[i] = allocMem( sizeof( char ) * blen );
			blk->D[i] = allocMem( sizeof( double ) * blen );
			blk->LT[i] = allocMem( sizeof( double ) * blen );
			blk->A[i].k = i;  
			for( j = 0; j < blen; j++ ) {
				blk->C[i][j] = i == j ? 1 : (rand() % 2 ? 1 : 0); blk->nC += blk->C[i][j];
				blk->R[i][j] = rand() % blen;
				blk->D[i][j] = MINDELAY + timescale * -log( RANDPROB );
				blk->LT[i][j] = RANDPROB;
			}
		} 
	}
}

eventType *qV( blockType *blk, arcType *a ) {
	eventType *v, *nv, *lv = NULL;
	
	// find where to slot in this event
	v = q.farthest; 
	while( v != NULL ) {
		if( a->t > v->a->t ) break;
		else { lv = v; v = v->prev; }
	}
	// make new event and link it into the queue
	nv = allocMem( vsize );
	if( (nv->prev = v) != NULL ) v->next = nv;
	else q.nearest = nv;
	if( (nv->next = lv) != NULL ) lv->prev = nv;
	else q.farthest = nv;

	// carry route arc info with the event,
	// for adapting arcs to converge on external bits and thereby predict them
	nv->a = a; nv->blk = blk; q.n++; 
	return nv;
}

// the main event function, which is the core theory.
// everything else is supportive for a given simulation
void fV( eventType *v ) {
	unsigned int **R, nb = 0, vk, ak;
	arcType *a, *av, *akp, *prev = NULL, *oldp = NULL, *newp = NULL;
	double **D, **LT, ld, oldest, newest = 0.0;
	char **C, s3new = 0, s3old = 0, s3ak = 0, rr = 0;
	blockType *blk;
	
	// local vars used for readability and speed, and update the global time
	blk = v->blk; D = blk->D; R = blk->R; C = blk->C; LT = blk->LT;
	vk = v->a->k; t = v->a->t; oldest = t; akp = &blk->A[vk]; s3ak = akp->s3;

	// if bit arrives at an occupied location, trigger a push/spike event
	if( s3ak > 0 ) { nump++; a = blk->As3; 
		// shorten delay if INTBIT arrives late
		if( v->a->s3 == INTBIT && s3ak == EXTBIT && toggle ) {
			if( (ld = D[v->a->i][v->a->j] - (t - akp->t) - MINDELAY) > MINDELAY ) {
				D[v->a->i][v->a->j] = ld; toggle = 1 - toggle; sumrr++;
			} 
		}
		// record time when a routed INTBIT makes a correct prediction
		if( v->a->s3 == EXTBIT && s3ak == INTBIT ) LT[akp->i][akp->j] = t;
		// go through linked list of locations with bits
		while( a ) { ak = a->k;
			// alter couplings only when pushed by an INTBIT
			if( blk->learn && v->a->s3 == INTBIT && s3ak == INTBIT && ak != vk ) {
				if( a->t < oldest ) { oldest = a->t; oldp = a; s3old = a->s3; }				
				if( a->t > newest ) { newest = a->t; newp = a; s3new = a->s3; }
			}
			// if triggered location vk is linked to location ak, push the bit
			if( C[vk][ak] ) { 
				// create an arc for the event to be queued, set its values, and queue it
				av = allocMem( sizeof( arcType ) );
				av->d = D[vk][ak]; av->t = t + av->d; av->s3 = INTBIT; 
				av->i = vk; av->j = ak; av->k = R[vk][ak]; qV( blk, av ); 
				// remove bit and remove pushed arc from the linked list 
				a->s3 = 0;
				if( prev != NULL ) prev->next = a->next; 
				else blk->As3 = a->next; 
				printf( "bc %f %d %d\n", t, vk==ak?-1:0, ak );
			// set prev if the bit was not routed
			} else { prev = a; }
			a = a->next; nb++;
		} 
		// push the oldest bit; its prediction has passed, time to move on
		// (self-couplings remain coupled, and don't alter where bit has already been rerouted)
		if( oldp ) { if( C[vk][oldp->k] == 0 && s3old == INTBIT ) { 
			C[vk][oldp->k] = 1; blk->nC++; } }
		// do not push the newest bit; give it a chance to generate a spike
		if( newp ) { if( C[vk][newp->k] == 1 && s3new == INTBIT ) { 
			C[vk][newp->k] = 0; blk->nC--; } }
		
		// print out simulation info if flag is set
		if( blk->print ) printf( "%f %d %d %d %d %d %d %d %d %d %f %f %d %d\n", t, nump, blk->id,  
			blk->nC, v->a->s3, numExt0, sumrr, v->a->i, v->a->j, vk, t - akp->t, v->a->d, nb, q.n );

		// queue the trigger bit if there is an external block link, otherwise delete
		if( blk->Blink ) { 
			// update arc info for the trigger bit
			v->a->d = MINDELAY + D[vk][vk]; v->a->t = t + v->a->d; v->a->s3 = EXTBIT;
			// send the trigger bit to the external block
			qV( blk->Blink, v->a ); 
		} else free( v->a ); 
		numExt0 = 0; sumrr = 0;
	// if bit arrives at an unoccupied location, set state and update arc info at location
	} else { if( v->a->s3 == EXTBIT ) { numExt0++;  
		if( blk->learn && !toggle ) { a = blk->As3;
			while( a ) { if( a->s3 == INTBIT && !a->rr && LT[a->i][a->j] < oldest ) 
				{ oldest = LT[a->i][a->j]; oldp = a; } a = a->next; }
			if( oldp ) { oldp->rr = 1; rr = 1; sumrr++; toggle = 1 - toggle;
				R[oldp->i][oldp->j] = vk; 
				D[oldp->i][oldp->j] = D[oldp->i][oldp->j] + (t - oldp->t) - MINDELAY;
			}			
		} }
		printf( "bc %f %d %d\n", t, v->a->s3, v->a->k );
		akp->i = v->a->i; akp->j = v->a->j;  
		akp->t = t; akp->d = v->a->d; akp->s3 = v->a->s3; akp->rr = rr;
		// add arc w/ bit to linked list, and free event arc
		akp->next = blk->As3; blk->As3 = akp; free( v->a );
	}
	
	// advance to the next event in the queue, and remove the current one
	if( (q.nearest = v->next) == NULL ) q.farthest = NULL;
	else q.nearest->prev = NULL;
	free( v ); q.n--; 
}

//////////////////////////////////////////////////////////

void generateBlock( blockType *blk, unsigned int maxp ) {
	arcType *a;
	eventType *v, *lv;
	unsigned int i;
	blockType *tmp;
	
/*	still figuring out how to run as a generative model
*/
}

void perturbBlock( blockType *blk, unsigned int maxp ) {
	arcType *a;
	eventType *v, *lv;
	
	nump = 0;
	// add input bits one at a time block bnum, each time running 
	// forward until the next input bit.
	while( nump < maxp ) { 
		// set values of the external arc data structure
		a = allocMem( sizeof( arcType ) );
		a->s3 = EXTBIT; a->k = rand() % blen; 
		// advance time forward by a random (Poisson) increment
		a->d = MINDELAY + timescale * -log( RANDPROB ); a->t = t + a->d; 
		// loop until queue is empty, set number of pushes is reached, 
		// or the next random event is reached.
		lv = qV( blk, a );
		while( (v = q.nearest) != NULL && nump < maxp ) {
			fV( v ); if( v == lv ) break; }		
	}
}

void sequenceBlock( blockType *blk, unsigned int maxp ) {
	arcType *a;
	eventType *v, *lv;
	double *d;
	unsigned int i;
	int w;
	// make a sequence of random (Poisson) delays to loop through 
	d = allocMem( sizeof( double ) * blen );
	for( i = 0; i < blen; i++ ) d[i] = MINDELAY + timescale * -log( RANDPROB );
	
	i = 0;
	while( nump < maxp ) { 
		a = allocMem( sizeof( arcType ) );
		a->s3 = EXTBIT; a->k = i; a->d = d[i]; a->t = t + a->d; 
		lv = qV( blk, a );
		while( (v = q.nearest) != NULL && nump < maxp ) {
			fV( v ); if( v == lv ) break; }
		if( ++i == blen ) i = 0;
	}
}

void walkBlock( blockType *blk, unsigned int maxp ) {
	arcType *a;
	eventType *v, *lv;
	double *d;
	unsigned int i;
	int w;

	d = allocMem( sizeof( double ) * blen );
	for( i = 0; i < blen; i++ ) d[i] = MINDELAY + timescale * -log( RANDPROB );
	
	i = 0;
	while( nump < maxp ) { 
		a = allocMem( sizeof( arcType ) );
		a->s3 = EXTBIT; a->k = i; a->d = d[i]; a->t = t + a->d; 
		lv = qV( blk, a );
		while( (v = q.nearest) != NULL && nump < maxp ) {
			fV( v ); if( v == lv ) break; }
		w = rand() % 2 ? 1 : -1; i = i == 0 && w < 0 ? blen-1 : (i == blen-1 && w > 0 ? 0 : i + w);
	}
}

void comboBlock( blockType *blk, unsigned int maxp ) {
	arcType *a;
	eventType *v, *lv1 = NULL, *lv2 = NULL;
	unsigned int i; int w;

	if( delay == NULL ) {
		delay = allocMem( sizeof( double ) * blen );
		for( dd = 0; dd < blen; dd++ ) delay[dd] = MINDELAY + timescale * -log( RANDPROB );
		dd = rand() % (blen/2);
	} i = blen/2 + (rand() % (blen/2));
	while( nump < maxp ) { 
		if( !lv1 ) { a = allocMem( sizeof( arcType ) );
			a->s3 = EXTBIT; a->k = dd; a->d = delay[dd]; a->t = t + a->d; 
			lv1 = qV( blk, a );
		}
		if( !lv2 ) { a = allocMem( sizeof( arcType ) );
			a->s3 = EXTBIT; a->k = i; a->d = delay[i]; a->t = t + a->d; 
			lv2 = qV( blk, a );
		}
		while( (v = q.nearest) != NULL && nump < maxp ) {
			fV( v ); if( v == lv1 || v == lv2 ) break; }
		if( v == lv1 ) { w = rand() % 2 ? 1 : -1; lv1 = NULL;
			dd = dd == 0 && w < 0 ? (blen/2)-1 : (dd == (blen/2)-1 && w > 0 ? 0 : dd + w); }
		if( v == lv2 ) { if( ++i >= blen ) i = blen/2; lv2 = NULL; }
	}
}

int main( int numArgs, char **args ) {
	unsigned int n, bnum, maxp, b1, b2, bp;
	FILE *fptr;
	char vname[256];
	
	srand( time( NULL ) ); q.n = 0;

	if( (fptr = fopen( args[1], "rt" )) == NULL )
		{ printf( "file not found %s\n", args[1] ); exit(0); }

	while( fscanf( fptr, "%s ", vname ) == 1 ) {
		if( !stricmp( vname, "blockLength" ) ) fscanf( fptr, "%d\n", &blen );
		if( !stricmp( vname, "timescale" ) ) fscanf( fptr, "%lf\n", &timescale ); 
		if( !stricmp( vname, "addBlocks" ) ) { fscanf( fptr, "%d\n", &n ); addBlocks( n ); }
		if( !stricmp( vname, "link" ) ) { fscanf( fptr, "%d %d\n", &b1, &b2 ); 
			Barray[b1].Blink = &Barray[b2]; }
		if( !stricmp( vname, "learnOn" ) ) { fscanf( fptr, "%d\n", &bnum ); Barray[bnum].learn = 1;	}
		if( !stricmp( vname, "learnOff" ) ) { fscanf( fptr, "%d\n", &bnum ); Barray[bnum].learn = 0;	}
		if( !stricmp( vname, "printOn" ) ) { fscanf( fptr, "%d\n", &bnum ); Barray[bnum].print = 1;	}
		if( !stricmp( vname, "printOff" ) ) { fscanf( fptr, "%d\n", &bnum ); Barray[bnum].print = 0;	}
		if( !stricmp( vname, "perturb" ) ) { fscanf( fptr, "%d %d\n", &bnum, &maxp ); 
			perturbBlock( &Barray[bnum], maxp ); 
		}
		if( !stricmp( vname, "walk" ) ) { fscanf( fptr, "%d %d\n", &bnum, &maxp ); 
			walkBlock( &Barray[bnum], maxp ); 
		}
		if( !stricmp( vname, "sequence" ) ) { fscanf( fptr, "%d %d\n", &bnum, &maxp ); 
			sequenceBlock( &Barray[bnum], maxp ); 
		}
		if( !stricmp( vname, "generate" ) ) { fscanf( fptr, "%d %d\n", &bnum, &maxp );
			generateBlock( &Barray[bnum], maxp ); 
		}
		if( !stricmp( vname, "combo" ) ) { fscanf( fptr, "%d %d\n", &bnum, &maxp ); 
			comboBlock( &Barray[bnum], maxp ); 
		}
	}
	fclose(fptr);
}
