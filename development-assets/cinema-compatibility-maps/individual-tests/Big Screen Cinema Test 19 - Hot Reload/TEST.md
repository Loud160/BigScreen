# Test 19: Cinema mapper-config hot reload

Expected behavior:

- Starts as a centered flat screen
- Apply the supplied variant while playing
- Expected: screen moves left, curves and changes color in place

Special procedure:

- Copy hot-reload-variant.json over cinema-video.json while this map is running.
- Restart the map afterward to restore the original configuration.

The difficulty intentionally contains no notes, bombs, walls, arcs,
chains, or gameplay objects. Lighting pulses are present to make bloom
and transparency behavior observable.
