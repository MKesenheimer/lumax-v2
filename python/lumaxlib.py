"""
Define the C-variables and functions from the C-files that are needed in Python
"""
from ctypes import *
import sys
import math
import signal
import numpy

DEBUG = 0

def restrict(x, minx, maxx):
    return max(min(maxx, x), minx)

class _lpoint(Structure):
    #_pack_=2
    _fields_ = [("x", c_uint16),
                ("y", c_uint16),
                ("r", c_uint16),
                ("g", c_uint16),
                ("b", c_uint16),
                ("Ch6", c_uint16),
                ("Ch7", c_uint16),
                ("Ch8", c_uint16),
                ("TTL", c_uint16)]
c_point_p = POINTER(_lpoint)

class _lpoints(Structure):
    #_pack_=2
    _fields_ = [('length', c_int),
                # an array of structs
                ('struct_arr', c_point_p)]

    def __init__(self, num_of_structs):
        elems = (_lpoint * num_of_structs)()
        self.struct_arr = cast(elems, c_point_p)
        self.length = num_of_structs

        for num in range(0, num_of_structs):
            self.struct_arr[num].x = 0
            self.struct_arr[num].y = 0
            self.struct_arr[num].r = 0
            self.struct_arr[num].g = 0
            self.struct_arr[num].b = 0

class shape:
    def __init__(self, points=None, color=None):
        """
        constructor.
        empty:            shape()
        colored points:   shape(points)        points: (npoints, 5) array (x, y, r, g, b)
        coords + color:   shape(points, color) points: (npoints, 2), color: (npoints, 3) or (1, 3)
        """
        if points is None:
            self.points = numpy.empty((0, 5), dtype='uint16')
            self.npoints = 0
            return

        if color is None:
            self.points = points
            self.npoints = len(points)
            return

        """
        Alternative:
        define a shape by numpy arrays for coordinates and color.
        points: for example: numpy.array([[0, 1], [2, 3], [4, 5]]) 
        color: for example: numpy.array([[128, 255, 128], [255, 255, 128], [128, 255, 255]]) 
        note: points and color must be equal in size, or color must be of size 1 (one color for all points)
        """
        plength = len(points)
        clength = len(color)
        if plength == 0 or clength == 0:
            raise Exception("[ERROR] Arrays must not be empty.")

        if plength != clength and clength != 1:
            raise Exception("[ERROR] Points and color array must be equal in size, or color array must be of size 1.") 

        self.points = numpy.empty((plength, 5), dtype='uint16')
        for i in range(0, plength):
            self.points[i, 0] = points[i, 0]
            self.points[i, 1] = points[i, 1]
            self.points[i, 2] = color[i % clength, 0]
            self.points[i, 3] = color[i % clength, 1]
            self.points[i, 4] = color[i % clength, 2]

        self.npoints = plength

    def get_points(self):
        return self.points

    def get_number_of_points(self):
        return self.npoints

class geometry:
    @staticmethod
    def __circle_point(x, y, r, i, npoints):
        th = 2 * math.pi / npoints * i
        xunit = int(r * math.cos(th) + x)
        yunit = int(r * math.sin(th) + y)
        return xunit, yunit

    @staticmethod
    def circle(x0, y0, r, npoints, rd, gr, bl):
        points = numpy.empty((npoints, 2), dtype='uint16')
        colors = numpy.array([[rd, gr, bl]])
        for i in range(0, npoints):
            x, y = geometry.__circle_point(x0, y0, r, i, npoints)
            points[i, 0] = x
            points[i, 1] = y
        return shape(points, colors)

    @staticmethod
    def line(x0, y0, x1, y1, npoints, rd, gr, bl):
        if npoints < 2:
            npoints = 2
        lx = numpy.linspace(x0, x1, npoints)
        ly = numpy.linspace(y0, y1, npoints)
        points = numpy.empty((npoints, 5), dtype='uint16')
        for i in range(0, npoints):
            points[i, 0] = lx[i]
            points[i, 1] = ly[i]
            points[i, 2] = rd
            points[i, 3] = gr
            points[i, 4] = bl
        return shape(points)

    @staticmethod
    def triangle(x0, y0, x1, y1, x2, y2, npoints, rd, gr, bl):
        if npoints < 2:
            npoints = 2
        lx1 = numpy.linspace(x0, x1, npoints)
        ly1 = numpy.linspace(y0, y1, npoints)
        lx2 = numpy.linspace(x1, x2, npoints)
        ly2 = numpy.linspace(y1, y2, npoints)
        lx3 = numpy.linspace(x2, x0, npoints)
        ly3 = numpy.linspace(y2, y0, npoints)
        points = numpy.empty((3 * npoints - 2, 5), dtype='uint16')
        index = 0
        for i in range(0, npoints - 1):
            points[index, 0] = lx1[i]
            points[index, 1] = ly1[i]
            points[index, 2] = rd
            points[index, 3] = gr
            points[index, 4] = bl
            index += 1
        for i in range(0, npoints - 1):
            points[index, 0] = lx2[i]
            points[index, 1] = ly2[i]
            points[index, 2] = rd
            points[index, 3] = gr
            points[index, 4] = bl
            index += 1
        for i in range(0, npoints):
            points[index, 0] = lx3[i]
            points[index, 1] = ly3[i]
            points[index, 2] = rd
            points[index, 3] = gr
            points[index, 4] = bl
            index += 1
        return shape(points)

    @staticmethod
    def tetragon(x0, y0, x1, y1, x2, y2, x3, y3, npoints, rd, gr, bl):
        if npoints < 2:
            npoints = 2
        lx1 = numpy.linspace(x0, x1, npoints)
        ly1 = numpy.linspace(y0, y1, npoints)
        lx2 = numpy.linspace(x1, x2, npoints)
        ly2 = numpy.linspace(y1, y2, npoints)
        lx3 = numpy.linspace(x2, x3, npoints)
        ly3 = numpy.linspace(y2, y3, npoints)
        lx4 = numpy.linspace(x3, x0, npoints)
        ly4 = numpy.linspace(y3, y0, npoints)
        points = numpy.empty((4 * npoints - 3, 5), dtype='uint16')
        index = 0
        for i in range(0, npoints - 1):
            points[index, 0] = lx1[i]
            points[index, 1] = ly1[i]
            points[index, 2] = rd
            points[index, 3] = gr
            points[index, 4] = bl
            index += 1
        for i in range(0, npoints - 1):
            points[index, 0] = lx2[i]
            points[index, 1] = ly2[i]
            points[index, 2] = rd
            points[index, 3] = gr
            points[index, 4] = bl
            index += 1
        for i in range(0, npoints - 1):
            points[index, 0] = lx3[i]
            points[index, 1] = ly3[i]
            points[index, 2] = rd
            points[index, 3] = gr
            points[index, 4] = bl
            index += 1
        for i in range(0, npoints):
            points[index, 0] = lx4[i]
            points[index, 1] = ly4[i]
            points[index, 2] = rd
            points[index, 3] = gr
            points[index, 4] = bl
            index += 1
        return shape(points)

    @staticmethod
    def rotate_shape(shape1, pivot, angle_deg):
        points = shape1.get_points()
        if DEBUG:
            print("[DEBUG] points = {}".format(points[0]))
        # This code was generated by an AI (https://chat.openai.com/chat)
        angle_radians = math.radians(angle_deg)
        cos_angle = math.cos(angle_radians)
        sin_angle = math.sin(angle_radians)
        # Translate points so that the pivot point is at the origin
        translated_points = [(point[0] - pivot[0], point[1] - pivot[1], point[2], point[3], point[4]) for point in points]
        # Rotate each point around the origin
        rotated_points = [(cos_angle * point[0] - sin_angle * point[1], sin_angle * point[0] + cos_angle * point[1], point[2], point[3], point[4]) for point in translated_points]
        # Translate points back to their original position
        rotated_points = [(point[0] + pivot[0], point[1] + pivot[1], point[2], point[3], point[4]) for point in rotated_points]
        rotated_points = numpy.array(rotated_points, dtype='uint16')
        if DEBUG:
            print("[DEBUG] len(rotated_points) = {}".format(len(rotated_points)))
            print("[DEBUG] rotated_points = {}".format(rotated_points[0]))
        return shape(rotated_points)


class lumax:
    # The low-level driver state (the loaded CDLL, the byte-capture log file,
    # the blanking delay, and the exit-safety net) is process-wide in the C
    # library, so every handle in this process shares one `lumax` instance.
    # `lumax()` always returns the same object, which keeps the exit-safety
    # net aware of EVERY open device at once (laser safety: no beam left on).
    _instance = None

    def __new__(cls):
        if cls._instance is None:
            inst = super().__new__(cls)
            inst._initialized = False
            cls._instance = inst
        return cls._instance

    def __init__(self):
        if self._initialized:
            return
        self._initialized = True
        import os
        # Determine library path relative to this file
        base_dir = os.path.abspath(os.path.dirname(__file__))
        # Try platform-specific shared library (.so for Linux, .dylib for macOS)
        lib_name = f'liblumax_{sys.platform}.so'
        self.lib_path = os.path.join(base_dir, 'libs', lib_name)
        try:
            self.lumax_lib = CDLL(self.lib_path)
        except OSError:
            # Fallback to macOS .dylib naming if .so not found
            dylib_name = 'liblumax.dylib'
            self.lib_path = os.path.join(base_dir, 'libs', dylib_name)
            try:
                self.lumax_lib = CDLL(self.lib_path)
            except OSError:
                # Try Windows DLL if running on Windows (unlikely in this env)
                win_path = os.path.join(base_dir, 'libs', 'liblumax_windows.dll')
                try:
                    self.lumax_lib = CDLL(win_path)
                except OSError:
                    print(f'[ERROR] OS {sys.platform} not recognized or library not found at {self.lib_path}')

        c_int_p = POINTER(c_int)

        self.lumax_lib.Lumax_GetApiVersion.argtypes = None
        self.lumax_lib.Lumax_GetApiVersion.restype = c_float

        self.lumax_lib.Lumax_GetPhysicalDevices.argtypes = None
        self.lumax_lib.Lumax_GetPhysicalDevices.restype = c_int

        self.lumax_lib.Lumax_OpenDevice.argtypes = (c_int, c_int)
        self.lumax_lib.Lumax_OpenDevice.restype = c_void_p

        self.lumax_lib.Lumax_SetTTL.argtypes = (c_void_p, c_int)
        self.lumax_lib.Lumax_SetTTL.restype = c_int

        self.lumax_lib.Lumax_WaitForBuffer.argtypes = (c_void_p, c_int, c_int_p, c_int_p)
        self.lumax_lib.Lumax_WaitForBuffer.restype = c_int

        self.lumax_lib.Lumax_SendFrame.argtypes = (c_void_p, c_point_p, c_int, c_int, c_int, c_int_p)
        self.lumax_lib.Lumax_SendFrame.restype = c_int

        self.lumax_lib.Lumax_StopFrame.argtypes = (c_void_p,)
        self.lumax_lib.Lumax_StopFrame.restype = c_int

        self.lumax_lib.Lumax_CloseDevice.argtypes = (c_void_p,)
        self.lumax_lib.Lumax_CloseDevice.restype = c_int

        self.lumax_lib.Lumax_SetBlankingDelay.argtypes = (c_int,)
        self.lumax_lib.Lumax_SetBlankingDelay.restype = None

        # open devices: numDev -> handle (the C library supports up to 8
        # cards). Used by the exit safety net (stops/closes EVERY open
        # device) and to make stop_frame/close_device idempotent per handle.
        self._open_handles = {}

    def get_api_version(self):
        return float(self.lumax_lib.Lumax_GetApiVersion())

    def get_physical_devices(self):
        return int(self.lumax_lib.Lumax_GetPhysicalDevices())

    def open_device(self, numDev, channel):
        try:
            handle = int(self.lumax_lib.Lumax_OpenDevice(c_int(numDev), c_int(channel)))
        except:
            return 0
        if handle:
            # remember the handle and install the exit safety net.
            # The C library already installs an atexit/signal backstop
            # at open time; these Python-level handlers do the same
            # cleanup for EVERY open device, then restore the normal
            # exit semantics (KeyboardInterrupt on Ctrl+C).
            self._open_handles[numDev] = handle
            self._install_exit_handlers()
        return handle

    def _install_exit_handlers(self):
        def on_exit_signal(signum, frame):
            print("[lumaxlib] got signal %d: blanking beam and closing %d device(s)"
                  % (signum, len(self._open_handles)), flush=True)
            # Stop + TTL off + close EVERY open device so no beam is left on,
            # then restore normal exit semantics (KeyboardInterrupt on Ctrl+C).
            for dev, handle in list(self._open_handles.items()):
                if handle:
                    self.lumax_lib.Lumax_StopFrame(c_void_p(handle))
                    self.lumax_lib.Lumax_SetTTL(c_void_p(handle), c_int(0))
                    self.lumax_lib.Lumax_CloseDevice(c_void_p(handle))
            self._open_handles.clear()
            if signum == signal.SIGINT:
                raise KeyboardInterrupt
            raise SystemExit(128 + signum)
        try:
            signal.signal(signal.SIGINT, on_exit_signal)
            signal.signal(signal.SIGTERM, on_exit_signal)
            signal.signal(signal.SIGHUP, on_exit_signal)
        except ValueError:
            # not in the main thread; the C-level backstop still applies
            pass

    def setTTL(self, handle, ttl):
        return int(self.lumax_lib.Lumax_SetTTL(c_void_p(handle), c_int(ttl)))

    def wait_for_buffer(self, handle, timeOut):
        timeToWait = c_int(0)
        bufferChanged = c_int(0)
        ret = self.lumax_lib.Lumax_WaitForBuffer(c_void_p(handle), c_int(timeOut), byref(timeToWait), byref(bufferChanged))
        return int(ret), int.from_bytes(timeToWait, byteorder='big', signed=True), int.from_bytes(bufferChanged, byteorder='big', signed=True)

    def send_frame(self, handle, points, scanSpeed, updateMode):
        numOfPoints = points.length
        timeToWait = c_int(0)
        ret = self.lumax_lib.Lumax_SendFrame(c_void_p(handle), points.struct_arr, c_int(numOfPoints), c_int(scanSpeed), c_int(updateMode), byref(timeToWait))
        return int(ret), int.from_bytes(timeToWait, byteorder='big', signed=True)

    def stop_frame(self, handle):
        # no-op for a stale handle (e.g. after the exit safety net
        # already stopped and closed the device)
        if not handle or handle not in self._open_handles.values():
            return 0
        return int(self.lumax_lib.Lumax_StopFrame(c_void_p(handle)))

    def close_device(self, handle):
        if not handle or handle not in self._open_handles.values():
            return 0
        for dev, h in list(self._open_handles.items()):
            if h == handle:
                del self._open_handles[dev]
                break
        return int(self.lumax_lib.Lumax_CloseDevice(c_void_p(handle)))

    def set_logfile(self, path):
        """Enable byte-level protocol capture (hex dump) to the given file.
        Pass an empty string to disable it (the default).
        If the underlying library does not expose ``Lumax_SetLogFile`` this
        becomes a no‑op (silently ignored)."""
        # Safely attempt to locate the function; some builds omit it.
        try:
            setlog = self.lumax_lib.Lumax_SetLogFile
        except AttributeError:
            if DEBUG:
                print('[lumaxlib] Lumax_SetLogFile not available in this library')
            return
        # Configure the ctypes prototype if not already set.
        setlog.argtypes = [c_char_p]
        setlog.restype = None
        setlog((path or "").encode())

    def set_blanking_delay(self, delay_ms):
        """Set the blanking delay in milliseconds.
        At each off->on light transition, this many ms worth of blank
        (laser-off) points are inserted at the same position.
        At each on->off transition, extra 'on' points are added before
        the laser turns off.
        delay_ms: blanking delay in milliseconds (0 = disabled, default)."""
        self.lumax_lib.Lumax_SetBlankingDelay(c_int(delay_ms))

class lumax_renderer:
    def __init__(self, numDev = 1, mirrorx = 0, mirrory = 0):
        # numDev: which card to drive (1..8, selected by ascending USB serial
        # in the C library). Use separate renderers for separate cards.
        # mirrorx / mirrory: 0 or 1, flip the drawn pattern in x / y.
        self.lmx = lumax()
        ret = self.lmx.get_api_version()
        ret1 = self.lmx.get_physical_devices()
        self.numDev = numDev
        self.lhandle = self.lmx.open_device(numDev, 0)
        if not self.lhandle:
            raise RuntimeError("[ERROR] Could not open Lumax device %d "
                               "(%d device(s) found). Is the card connected?"
                               % (numDev, ret1))
        ret2, timeToWait, bufferChanged = self.lmx.wait_for_buffer(self.lhandle, 17)
        if DEBUG:
            print("[DEBUG] API version: {}".format(ret))
            print("[DEBUG] Number of physical devices: {}".format(ret1))
            print("[DEBUG] Lumax handle (device {}): {}".format(numDev, self.lhandle))
            #print("[DEBUG] SetTTL return: {}".format(lumax.setTTL(self.lhandle, 0)))
            print("[DEBUG] WaitForBuffer return: {}, {}, {}".format(ret2, timeToWait, bufferChanged))
        self.shapes = numpy.array([])
        self.totnpoints = 0
        self.mirrorx = mirrorx
        self.mirrory = mirrory
        self.blanking_delay_ms = 0

    # context manager: `with lumax_renderer(numDev) as r:` stops the frame and
    # closes the device when the block ends (including on exception/Ctrl+C),
    # so the beam is never left on.
    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.close_device()
        return False

    def set_blanking_delay(self, delay_ms):
        """Set the blanking delay in milliseconds.
        At each off->on light transition, this many ms worth of blank
        (laser-off) points are inserted at the same position.
        At each on->off transition, extra 'on' points are added before
        the laser turns off.
        delay_ms: blanking delay in milliseconds (0 = disabled, default)."""
        self.blanking_delay_ms = delay_ms
        self.lmx.set_blanking_delay(delay_ms)
        

    def new_frame(self):
        self.shapes = numpy.array([])
        self.totnpoints = 0

    def add_shape_to_frame(self, shape):
        self.shapes = numpy.append(self.shapes, shape)
        self.totnpoints += shape.get_number_of_points()

    def add_point_to_frame(self, point):
        if len(point) != 5:
            raise Exception("[ERROR] Point must be of dimension 5 (x, y, r, g, b)")
        xypoint = numpy.array([[point[0], point[1]]])
        color = numpy.array([[point[2], point[3], point[4]]])
        shp = shape(xypoint, color)
        lumax_renderer.add_shape_to_frame(self, shp)

    def add_points_to_frame(self, points):
        if len(points) == 0:
            raise Exception("[ERROR] Points must not be empty.")
        shp = shape(points)
        lumax_renderer.add_shape_to_frame(self, shp)


    def __generate_buffer(self):
        # two extra points for every shape + one extra point in the center of the frame
        nextrapoints = 2 * len(self.shapes) + 1
        buffer = _lpoints(self.totnpoints + nextrapoints)
        # start in the center of the screen
        index = 0
        buffer.struct_arr[index].x = 128 * 255
        buffer.struct_arr[index].y = 128 * 255
        buffer.struct_arr[index].r = 0
        buffer.struct_arr[index].g = 0
        buffer.struct_arr[index].b = 0

        for i in range(0, len(self.shapes)):
            p = self.shapes[i].get_points()
            # insert blank point at the beginning
            index += 1
            buffer.struct_arr[index].x = (255 * 255 * self.mirrorx) + (-1)**(self.mirrorx) * numpy.int32(p[0, 0])
            buffer.struct_arr[index].y = (255 * 255 * self.mirrory) + (-1)**(self.mirrory) * numpy.int32(p[0, 1])
            buffer.struct_arr[index].r = 0
            buffer.struct_arr[index].g = 0
            buffer.struct_arr[index].b = 0
            
            # copy the points
            for j in range(0, len(p)):
                index += 1
                buffer.struct_arr[index].x = (255 * 255 * self.mirrorx) + (-1)**(self.mirrorx) * numpy.int32(p[j, 0])
                buffer.struct_arr[index].y = (255 * 255 * self.mirrory) + (-1)**(self.mirrory) * numpy.int32(p[j, 1])
                buffer.struct_arr[index].r = p[j, 2]
                buffer.struct_arr[index].g = p[j, 3]
                buffer.struct_arr[index].b = p[j, 4]

            # insert blank point at the end
            index += 1
            buffer.struct_arr[index].x = (255 * 255 * self.mirrorx) + (-1)**(self.mirrorx) * numpy.int32(p[len(p) - 1, 0])
            buffer.struct_arr[index].y = (255 * 255 * self.mirrory) + (-1)**(self.mirrory) * numpy.int32(p[len(p) - 1, 1])
            buffer.struct_arr[index].r = 0
            buffer.struct_arr[index].g = 0
            buffer.struct_arr[index].b = 0

        return buffer

    def send_frame(self, pointrate : int):
        if len(self.shapes) == 0:
            raise Exception("Frame is empty.")

        buffer = lumax_renderer.__generate_buffer(self)

        # print the points
        if DEBUG:
            for i in range(0, buffer.length):
                print("p{} = {}, {}, {}, {}, {}".format(i, buffer.struct_arr[i].x, buffer.struct_arr[i].y, buffer.struct_arr[i].r, buffer.struct_arr[i].g, buffer.struct_arr[i].b))

        ret, timeToWait = self.lmx.send_frame(self.lhandle, buffer, pointrate, 0)
        if DEBUG:
            print("[DEBUG] SendFrame return: {}, {}".format(ret, timeToWait))
        ret, timeToWait, bufferChanged = self.lmx.wait_for_buffer(self.lhandle, 17)
        return

    def stop_frame(self):
        ret = self.lmx.stop_frame(self.lhandle)
        if DEBUG:
            print("[DEBUG] StopFrame return: {}".format(ret))
        return

    def close_device(self):
        ret = self.lmx.stop_frame(self.lhandle)
        ret1 = self.lmx.close_device(self.lhandle)
        if DEBUG:
            print("[DEBUG] StopFrame return: {}".format(ret))
            print("[DEBUG] CloseDevice return: {}".format(ret1))
        return