import time
import os

frames = []

#append all the ASCII animation frames from files to frames[] 
for i in range(2,46):
    #load ASCII frames from file
    fileName = "./chimney/frame" + str(i) + ".txt"
    
    with open(fileName) as f:
        contents = f.read()
        frames.append(contents)
        
        f.close()



#animate the image
i = 0

while True:
    print(frames[i % len(frames)], end="\r")
    time.sleep(.1)
    os.system('clear')#prevents line breaking while writing multiple lines in linux
    i += 1

