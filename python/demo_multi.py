from lumaxlib import *
import time
import sys

def main():
    # this demo drives two cards at once
    n = lumax().get_physical_devices()
    if n < 2:
        print("This demo needs 2 Minilumax cards (found %d). "
              "Connect a second card and re-run." % n)
        return 0

    # Two renderers, one per card. Each context manager stops the frame and
    # closes its device when the block ends (including on Ctrl+C / exception),
    # so no beam is left on. Card 2 uses mirrorx=1 to show the mirrorx/mirrory
    # constructor options.
    with lumax_renderer(1) as left, lumax_renderer(2, mirrorx=1) as right:
        # byte-level capture + blanking delay (both are process-wide settings)
        left.lmx.set_logfile('/tmp/lumax_demo_multi.log')
        left.set_blanking_delay(2)

        # card 1: red circle
        red = geometry.circle(128 * 255, 128 * 255, 5000, 100, 255 * 255, 0, 0)
        left.add_shape_to_frame(red)
        left.send_frame(10000)

        # card 2: green circle (mirrored in x)
        green = geometry.circle(128 * 255, 128 * 255, 5000, 100, 0, 255 * 255, 0)
        right.add_shape_to_frame(green)
        right.send_frame(10000)

        print("Frames sent to both cards. Running for 30s (Ctrl+C to stop).")
        time.sleep(30)

    # both devices stopped and closed here automatically
    print("Done; both devices closed.")
    return 0

# main program
if __name__ == '__main__':
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        # the lumax signal handler already blanked + closed every device;
        # swallow the interrupt for a clean exit
        print("\nStopped.")
        sys.exit(0)
