import os, sys
sys.path.append(os.path.abspath(os.path.dirname(__file__)))
from lumaxlib import *
import time
import math
import sys
import numpy

def main():
    # need at least one card
    if lumax().get_physical_devices() < 1:
        print("No Lumax device found. Connect a card and try again.")
        return 0

    # the context manager stops the frame and closes the device when the
    # block ends (including on Ctrl+C / exception), so the beam is never left on
    try:
        with lumax_renderer(1) as renderer:
            renderer.lmx.set_logfile('/tmp/lumax_demo.log')  # debug: byte-level capture

            # Set blanking delay to 2ms to compensate for galvanometer lag at high speeds
            # This inserts blank points at off->on transitions and extra on-points at on->off
            renderer.set_blanking_delay(2)

            # setup frame, generate a circle
            brigthness = 0.45 # 0 to 1
            r = int(brigthness * 255 * 255)
            g = int(brigthness * 255 * 255)
            b = int(brigthness * 255 * 255)
            r = restrict(r, 0, 255 * 255)
            g = restrict(g, 0, 255 * 255)
            b = restrict(b, 0, 255 * 255)
            circle = geometry.circle(128 * 255, 128 * 255, 5000, 100, r, g, b)
            renderer.add_shape_to_frame(circle)

            # add another circle
            circle1 = geometry.circle(150 * 255, 150 * 255, 500, 10, 0, 255 * 255, 0)
            renderer.add_shape_to_frame(circle1)

            # add a point
            point = numpy.array([128 * 255, 128 * 255, 255 * 255, 255 * 255, 255 * 255])
            renderer.add_point_to_frame(point)

            # send the frame to the device
            renderer.send_frame(10000)

            # wait
            time.sleep(1)

            # new frame
            renderer.new_frame()

            # add multiple points (triangle)
            points = numpy.array([[190 * 255, 170 * 255, 0, 255 * 255, 255 * 255],
                                  [190 * 255, 190 * 255, 0, 255 * 255, 255 * 255],
                                  [200 * 255, 190 * 255, 0, 255 * 255, 255 * 255],
                                  [190 * 255, 170 * 255, 0, 255 * 255, 255 * 255]])
            renderer.add_points_to_frame(points)

            # add line with interpolated points
            line = geometry.line(160 * 255, 160 * 255, 178 * 255, 178 * 255, 5, 255 * 255, 255 * 255, 255 * 255)
            renderer.add_shape_to_frame(line)

            # add triangle
            triangle = geometry.triangle(100 * 255, 100 * 255, 156 * 255, 100 * 255, 128 * 255, 90 * 255, 10, 255 * 255, 255 * 255, 0)
            renderer.add_shape_to_frame(triangle)

            # add square
            square = geometry.tetragon(118 * 255, 118 * 255, 138 * 255, 118 * 255, 138 * 255, 138 * 255, 118 * 255, 138 * 255, 10, 255 * 255, 0, 255 * 255)
            renderer.add_shape_to_frame(square)

            # send the frame to the device
            renderer.send_frame(10000)

            # wait
            print("Running (Ctrl+C to stop).")
            time.sleep(100)

    except KeyboardInterrupt:
        # the lumax signal handler already blanked + closed the device
        print("\nExiting.")
    return 0

# main program
if __name__ == '__main__':
    sys.exit(main())
