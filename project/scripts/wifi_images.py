import cv2, sys

print("Collect 10-20 images with chess board mostly visible")
print("Be sure to move far into corners where warp is high\n")
print("Trying to connect (up to 30 sec) ...")

# use ESP32Cam wifi STA address with last byte from argument
if len(sys.argv) > 1:
  ipcam = "http://192.168.0." + sys.argv[1] + ":81/stream"
else:
  ipcam = "http://192.168.0.200:81/stream"
cap = cv2.VideoCapture(ipcam)  
if not cap.isOpened():
  print("  Could not find camera on wifi!")
else:
  print("\nOn image window: S to save particular image, ESC to quit ...")

num = 0
while cap.isOpened():

    success, img = cap.read()
    if not success:
      print("Stream broken!")
      break

    k = cv2.waitKey(5)
    if k == 27:
        break
    elif k == ord('s'): # wait for 's' key to save and exit
        name = "img" + str(num)
        cv2.imwrite('images/' + name + '.png', img)
        print("  saved %s" % (name))
        num += 1

    cv2.imshow('Img',img)

# clean up
cap.release()
cv2.destroyAllWindows()
print("Done")