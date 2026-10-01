import numpy as np
import cv2 as cv
import glob


######### FIND CHESSBOARD CORNERS - OBJECT POINTS AND IMAGE POINTS ###############

# specification of chessoard images
chessboardSize = (8,5)                 # one less than squares count
size_of_chessboard_squares_mm = 23     # measured on screen/paper
dims = (640,480)

# termination criteria
criteria = (cv.TERM_CRITERIA_EPS + cv.TERM_CRITERIA_MAX_ITER, 30, 0.001)

# prepare object points, like (0,0,0), (1,0,0), (2,0,0) ....,(6,5,0)
objp = np.zeros((chessboardSize[0] * chessboardSize[1], 3), np.float32)
objp[:,:2] = np.mgrid[0:chessboardSize[0],0:chessboardSize[1]].T.reshape(-1,2)
objp = objp * size_of_chessboard_squares_mm

# arrays to store object points and image points from all the images.
obj_pts = [] # 3d point in real world space
img_pts = [] # 2d points in image plane.

# go through all samples in the "images" subdirectory
print("Processing snapshots ...")
images = glob.glob('images/*.png')
for image in images:
    img = cv.imread(image)
    gray = cv.cvtColor(img, cv.COLOR_BGR2GRAY)

    # find the chess board corners
    ret, corners = cv.findChessboardCorners(gray, chessboardSize, None)
    print("  %d <- %s" % (ret, image))
    if ret == True:

        # add object points, image points (after refining them)
        obj_pts.append(objp)
        corners2 = cv.cornerSubPix(gray, corners, (11,11), (-1,-1), criteria)
        img_pts.append(corners2)

        # draw and display the corners
        cv.drawChessboardCorners(img, chessboardSize, corners2, ret)
        cv.imshow('img', img)
        cv.waitKey(1)


############## CALIBRATION #######################################################

# bulk of work
ret, cm, dist, rvecs, tvecs = cv.calibrateCamera(obj_pts, img_pts, dims, None, None)
#print("\nYields dist = ", end="")
#print(dist)

# convert to values for bottom up undistortion code
f  = 0.5 * (cm[0,0] + cm[1,1])
cx = cm[0,2]
cy = 479 - cm[1,2]
f2 = dist[0, 0]
f4 = dist[0, 1]
f6 = dist[0, 4]

# explain how to use
print("\nUse ocv_warp(%6.4f, %6.4f, %6.4f, %3.1f, 1.0, %3.1f, %3.1f)\n" % (f2, f4, f6, f, cx, cy))
print("Add or alter line in config/Baijiu_vals.ini:")
print("  grok_cam	 319.5 239.5 %3.1f 0 0 0 1 0\n" % (f))

"""
#==================================================
# average 140 deg lens values
print("Setting parameters to averages")
f  = 219
cx = 313.1
cy = 242.3
f2 =  0.1405
f4 = -0.1331 
f6 =  0.0249

# rebuild simpler cm and dist = [k1 k2 p1 p2 k3]
print("Radial-only flattening using single focal length ...")
cm[0,0] = f
cm[1,1] = f
cm[0,2] = cx
cm[1,2] = 479 - cy
dist[0,0] = f2         # k1
dist[0,1] = f4         # k2
dist[0,4] = f6         # k3
dist[0,2] = 0          # p1
dist[0,3] = 0          # p2

# show flattened versions of sample images
for image in images:
    img = cv.imread(image)
    h, w = img.shape[:2]
    flat = cv.undistort(img, cm, dist, None, cm)
    cv.imshow('img', flat)
    cv.waitKey(1000)
#==================================================
"""

# clean up
cv.destroyAllWindows()



