# frc-usb-camera
Ultra fast camera streaming server for Raspberry Pi - uses H.264 video encoder and UDP latency estimation algorithms to maximize video quality with minimal end to end latency

# Server-side setup

1. Clone the repository
2. Compile uvc_test.c. Make sure to include all important headers in the gcc command such as TurboJPEG, libuvc and libavcodec.
3. Run uvc_test. It should start listening on UDP port 1188 by default. Also, the default setup assumes two cameras, make sure they are connected.
4. Once this is working, you may wanna set it up as a systemd service so that it starts up automatically on boot.
5. For FRC applications, current rules require all wireless radios to be disabled. Also, it is highly recommended to set the partitions as read only to prevent data corruption due to sudden loss of power.


# Client-side setup
1. Clone the repository
2. Install all required Python libraries. These should be Twisted, SDL2 and PyAV. Pro tip: Last time I tried installing PyAV on Windows it wouldn't let me, so I had to install it using Conda.
3. You should be all set. Run python client.py. You may wanna set it up as a shortcut on the desktop for faster access during FRC comps. Also, create a folder named videos, the program will dump all received frames there for future reference. They're raw H264 frames, which you should be able to watch using VLC. 


# Known issues
1. The code is a mess. It started out as a quick proof-of-concept, but then it evolved into a full fledged video processor. Separation of concerns is awful here, as is the use of global variables. It'd be a good idea to rewrite it in something like C++, Rust or Go sometime in the future.
2. Due to time constraints, hardware video encoding wasn't implemented. This should enable 1080p30 streaming. Maybe in the C++ rewrite? Also, related: the red line in the middle is hardcoded in the client for a 720p resolution. We may wanna remove that if that's no longer useful in the 2020 challenge, or else determine the location and width programatically.
3. If you had dropped frames in your stream, the stored videos will probably look sped up. I may fix that in the future, but it'll take some work to emit a standard mp4 / mkv. Or also maybe just fill in the gaps with repeated frames.
4. Still not sure about the network fairness behavior. We saw some problems with lag/unresponsiveness in controls during FRC Houston Worlds 2019, but I'm pretty sure these were due to more voltage drop at the motors due to added weight and/or general field wireless issues. Salsify is supposed to behave fairly (I think?), and in any case, the default AP config sets a higher QoS for Driver Station packets. I believe UDP streams on port 1188 are deprioritized when bandwidth is scarce.
5. Another thing is that we don't implement the Salsify algorithm as described on the paper due to CPU usage constraints. In any case, we could actually enforce the allowed_to_send threshold as described, which I'm not entirely sure why I removed. Testing is needed, in any case.
