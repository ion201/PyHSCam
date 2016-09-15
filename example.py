import PyHSCam as cam

# Catch an error raised by a function call
try:
    cam.captureLiveImage(0)
except cam.CamRuntimeError as e:
    # This error will always be 2 - "PDC_ERROR_UNINITIALIZE"
    assert(e.errorCode == 2)

# Initialize module
cam.init()

# Open the device at a specified ip and save the returned id
iface_id = cam.openDeviceByIp('192.168.0.10')

# Set the resolution
cam.setResolution(iface_id, 1024, 1024)

# Get the current resolution
img_shape = cam.getCurrentResolution(iface_id)

# Set the capture rate to the lowest value possible
cap_rate = cam.getValidCapRates(iface_id)[0]
cam.setCapRate(iface_id, cap_rate)

# Record for 250 ms
cam.recordBlocking(iface_id, 250)

# Get the number of frames the were recorded
n_frames = cam.getMemoryFrameCount(iface_id)

# get the last frame - counting starts from 0!
img_bytes = cam.getImageFromMemory(iface_id, n_frames-1);

# Capture a live image
img_bytes = cam.captureLiveImage(iface_id)

# Convert the image to a 2d numpy array
try:
    import numpy as np
    img_array = np.ndarray(img_shape, 'uint8', img_bytes)
except ImportError:
    pass

# Save as a png with Pillow
try:
    from PIL import Image
    img = Image.frombytes('L', img_shape, img_bytes)
    img.save('captured.png')
    img.close()
except ImportError:
    pass

print('exiting...')
