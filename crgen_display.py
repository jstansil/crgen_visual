import pygame
pygame.init()

"""
--CONTROLS--
space: pause, and reset speed
s: increase fps
j: jump, moves 1000 lines up in file
l: leap, moves 50,000 lines up in file
"""

#colors: 255 is max value, format is (r, g, b)
white = (255,255,255) 
black = (0,0,0)
red = (255,0,0)
green = (0,255,0)
blue = (0,0,255)
yellow = (255, 255, 0)
gray = (240, 240, 240)
"""
white represents untouched space, where a bit hasn't been to yet
gray represents a location emptied by a push event
red represents external bits
green represents the location of a bit pushed by coupling
yellow represents the location of a bit pushed normally
"""


###edit this when changing the size of the array. default is 100
colsize = 100
###edit this to change the time scale that we search for changes. default is 0.5 
t_scale = 0.5
"""
need some experimenting to find what time scale value makes it the most obvious what is happening in the model.
just by visually looking through the output I saw most "waves" of change occur within 0.5 to 1 time units
but you can move this down to more accurately see changes at the cost of speed (or up for the opposite effect,
but past a value of 1 it skips alot and may not be useful).
"""


dheight = 600
bsize = int(dheight / colsize) - 1
dwidth = 800 + bsize
dheight += bsize
fps = 5
simExit = True
pause = False

display = pygame.display.set_mode((dwidth, dheight))
pygame.display.set_caption('Crgen Display')
clock = pygame.time.Clock()
net = open("output.txt", "r")


#grid display
matrix = []
#input column contains changes to the array
input_col = []
#store the locations of push events
pushes = []

#initialize matrix. block format: [color, x, y]
for x in range(dwidth, 0, -(bsize + 1)):
    column = []
    for y in range(0, colsize):
        column.append([white, x, (y*(bsize+1))])
    matrix.append(column)

cols = len(matrix)
rows = len(matrix[0])

#initialize input column
for j in range(0, rows):
   input_col.append(white)

#read the first line for comparison
#line format: [bc, time, event type, block #]
prev_line = net.readline().split()

def update(prev): #read from output and fill input column with changes
    #make sure we don't carry over push events. set them to gray after they've been displayed once
    for loc in pushes:
        input_col[loc] = gray
    pushes.clear()    
    while True: 
        curr_line = net.readline().split()
        if not curr_line: #end of file
            print("End of file!")
            simExit = False
            return
        if int(curr_line[2]) == -1:
            input_col[int(curr_line[3])] = yellow #pushed normally
            pushes.append(int(curr_line[3]))
        if int(curr_line[2]) == 0:
            input_col[int(curr_line[3])] = green #pushed via coupling
            pushes.append(int(curr_line[3]))
        if int(curr_line[2]) == 1:
            input_col[int(curr_line[3])] = black #internal bit
        if int(curr_line[2]) == 2:
            input_col[int(curr_line[3])] = red #external bit

        #if this condition is reached, we've consolidated all changes in the given timescale, so we terminate
        if (float(curr_line[1]) - float(prev[1])) > t_scale:
            prev = curr_line
            return prev
        
        prev = curr_line

def clear(): #empty input column
     for j in range(0, rows):
         input_col[j] = white

def jump(size): #move a distance in the output file
    for i in range(0, size):
        line = net.readline()
        if not line: #end of file
            print("End of file!")
            simExit = False
            return
    for col in matrix: #empty the matrix
        for block in col:
            block[0] = white
    prev_line = net.readline().split()

while simExit:
    #handle key presses for fps and pause
    for event in pygame.event.get():
        if event.type == pygame.QUIT:
            simExit = False
        if event.type == pygame.KEYDOWN:
            if event.key == pygame.K_SPACE:
                pause = not pause
                fps = 5
            if event.key == pygame.K_s:
                fps *= 2
            if event.key == pygame.K_j:
                jump(1000)
            if event.key == pygame.K_l:
                jump(50000)
            
    display.fill(white)
    y = 0
    #copy input column's blocks if they contain changes and display isn't paused
    if not pause:
        prev_line = update(prev_line)
        for block in matrix[0]:
            if input_col[y] != white:
                block[0] = input_col[y]
            y += 1
    #draw the current snapshot of the matrix
    for col in matrix:
        for block in col:
            pygame.draw.rect(display, block[0], [block[1], block[2], bsize, bsize])
    pygame.draw.line(display, black, (dwidth - bsize - 1, dheight - bsize - 1), (dwidth - bsize - 1, 0))
    pygame.display.update()
    #controls fps
    clock.tick(fps)

    #copies every row's colors backwards if display isnt paused
    if not pause:
        for a in range(cols-1, 0, -1):
            for b in range(rows-1, -1, -1):
                matrix[a][b][0] = matrix[a-1][b][0]
        clear()


net.close()
pygame.quit()
quit()

