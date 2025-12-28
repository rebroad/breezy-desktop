# Android and Raspberry Pi Client Options

## Question

I want to create a client for Raspberry Pi or Android (with DisplayPort output), or use an existing Android app. What are the options?

---

## Raspberry Pi Client

### Option 1: Custom Client Application (Recommended)

**Architecture:**
- Linux-based (Raspberry Pi OS)
- Receives MJPEG/H.264 stream via UDP/RTP
- Decodes using hardware acceleration (VideoCore GPU)
- Displays on connected HDMI/DisplayPort monitor

**Implementation Options:**

#### A. GStreamer Pipeline (Easiest)

**Advantages:**
- ✅ Simple pipeline-based approach
- ✅ Hardware acceleration support (VideoCore)
- ✅ Supports MJPEG, H.264, VP8
- ✅ Low latency configuration available
- ✅ Can output to Wayland/X11/DRM/KMS

**Example Pipeline (MJPEG):**
```bash
gst-launch-1.0 \
  udpsrc port=5000 caps="image/jpeg,width=1920,height=1080,framerate=60/1" ! \
  jpegdec ! \
  videoconvert ! \
  waylandsink sync=false  # or xvimagesink for X11
```

**Example Pipeline (H.264 with hardware decode):**
```bash
gst-launch-1.0 \
  udpsrc port=5000 caps="application/x-rtp,encoding-name=H264" ! \
  rtph264depay ! \
  h264parse ! \
  omxh264dec ! \  # Raspberry Pi hardware decoder
  videoconvert ! \
  waylandsink sync=false
```

**C/C++ Implementation:**
- Use GStreamer C API to build pipeline programmatically
- Handle network errors, reconnection
- Can add UI for connection management

#### B. VLC (Existing Solution)

**Advantages:**
- ✅ Already exists, no development needed
- ✅ Supports network streaming (RTP, HTTP)
- ✅ Hardware acceleration
- ✅ Can be controlled via command-line or API

**Disadvantages:**
- ⚠️ Buffering for smoothness (not ultra-low latency)
- ⚠️ Less control over latency settings
- ⚠️ May not achieve <50ms latency

**Usage:**
```bash
vlc rtp://@:5000
# or
vlc http://SERVER_IP:8080/stream.mjpg
```

#### C. MPV (Lightweight Alternative)

**Advantages:**
- ✅ Lightweight video player
- ✅ Supports network streaming
- ✅ Configurable buffering
- ✅ Good performance

**Usage:**
```bash
mpv --no-cache --framedrop=decoder rtp://@:5000
```

#### D. FFmpeg + SDL (Full Control)

**Advantages:**
- ✅ Full control over decoding/display
- ✅ Hardware acceleration support
- ✅ Customizable latency

**Implementation:**
- FFmpeg for decoding
- SDL2 for display
- UDP socket for network receiving

---

### Option 2: Simple HTTP MJPEG Stream

**Server-side:**
```python
# Python example
import socket
import struct
from http.server import BaseHTTPRequestHandler, HTTPServer

class MJPEGHandler(BaseHTTPRequestHandler):
    def do_GET(self):
        self.send_response(200)
        self.send_header('Content-Type', 'multipart/x-mixed-replace; boundary=--jpgboundary')
        self.end_headers()

        # Stream MJPEG frames
        while True:
            frame = get_next_frame()  # From DMA-BUF capture
            self.wfile.write(b'--jpgboundary\r\n')
            self.wfile.write(b'Content-Type: image/jpeg\r\n')
            self.wfile.write(f'Content-Length: {len(frame)}\r\n\r\n'.encode())
            self.wfile.write(frame)
```

**Client-side (Raspberry Pi):**
```bash
# Using VLC or browser
vlc http://SERVER_IP:8080/stream.mjpg
# or open in browser
```

**Advantages:**
- ✅ Very simple implementation
- ✅ Works with standard HTTP clients
- ✅ Can test in browser

**Disadvantages:**
- ⚠️ HTTP overhead (TCP)
- ⚠️ Higher latency than raw UDP
- ⚠️ Less efficient than RTP

---

## Android Client

### Option 1: Custom Android App (Recommended for Low Latency)

**Architecture:**
- Native Android app (Java/Kotlin)
- Receives MJPEG/H.264 stream via UDP/RTP
- Hardware-accelerated decoding using MediaCodec API
- Display to external display via DisplayPort/HDMI (USB-C)
- Uses Presentation API or MediaRouter for second screen

**Implementation Components:**

#### A. Network Receiving

**UDP Socket (Kotlin example):**
```kotlin
import java.net.DatagramPacket
import java.net.DatagramSocket

class StreamReceiver(private val port: Int) {
    private val socket = DatagramSocket(port)
    private val buffer = ByteArray(65536)  // Max UDP packet size

    fun receiveFrame(): ByteArray? {
        val packet = DatagramPacket(buffer, buffer.size)
        socket.receive(packet)
        return buffer.copyOf(packet.length)
    }
}
```

**HTTP MJPEG Stream (Alternative):**
```kotlin
import okhttp3.OkHttpClient
import okhttp3.Request
import java.io.BufferedInputStream

class MJPEGStreamReceiver(private val url: String) {
    fun startReceiving(callback: (ByteArray) -> Unit) {
        val client = OkHttpClient()
        val request = Request.Builder().url(url).build()
        val response = client.newCall(request).execute()

        val input = BufferedInputStream(response.body?.byteStream())
        // Parse MJPEG stream and call callback for each frame
    }
}
```

#### B. Hardware-Accelerated Decoding

**MediaCodec API (MJPEG):**
```kotlin
import android.media.MediaCodec
import android.media.MediaFormat
import android.view.Surface

class VideoDecoder(private val surface: Surface) {
    private var codec: MediaCodec? = null

    fun initMJPEG(width: Int, height: Int) {
        codec = MediaCodec.createDecoderByType("image/jpeg")
        val format = MediaFormat.createVideoFormat("image/jpeg", width, height)
        codec?.configure(format, surface, null, 0)
        codec?.start()
    }

    fun decodeFrame(frameData: ByteArray) {
        val inputBufferIndex = codec?.dequeueInputBuffer(0) ?: return
        val inputBuffer = codec?.getInputBuffer(inputBufferIndex)
        inputBuffer?.put(frameData)
        codec?.queueInputBuffer(inputBufferIndex, 0, frameData.size, 0, 0)

        // Process output
        val bufferInfo = MediaCodec.BufferInfo()
        var outputBufferIndex = codec?.dequeueOutputBuffer(bufferInfo, 0)
        while (outputBufferIndex != null && outputBufferIndex >= 0) {
            codec?.releaseOutputBuffer(outputBufferIndex, true)
            outputBufferIndex = codec?.dequeueOutputBuffer(bufferInfo, 0)
        }
    }
}
```

**MediaCodec API (H.264):**
```kotlin
fun initH264(width: Int, height: Int) {
    codec = MediaCodec.createDecoderByType("video/avc")  // H.264
    val format = MediaFormat.createVideoFormat("video/avc", width, height)
    codec?.configure(format, surface, null, 0)
    codec?.start()
}
```

#### C. DisplayPort/HDMI Output

**Presentation API (Recommended):**
```kotlin
import android.app.Presentation
import android.content.Context
import android.os.Bundle
import android.view.Display

class StreamPresentation(
    context: Context,
    display: Display
) : Presentation(context, display) {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        val surfaceView = SurfaceView(context)
        setContentView(surfaceView)

        // Use Surface from SurfaceView for MediaCodec
        val surface = surfaceView.holder.surface
        val decoder = VideoDecoder(surface)
        // ... start receiving and decoding
    }
}

// In Activity:
val displayManager = getSystemService(Context.DISPLAY_SERVICE) as DisplayManager
val displays = displayManager.displays

// Find external display (DisplayPort/HDMI)
val externalDisplay = displays.find { it.flags and Display.FLAG_PRESENTATION != 0 }

externalDisplay?.let {
    val presentation = StreamPresentation(this, it)
    presentation.show()
}
```

**MediaRouter (Alternative):**
```kotlin
import android.media.MediaRouter

val mediaRouter = getSystemService(Context.MEDIA_ROUTER_SERVICE) as MediaRouter
val route = mediaRouter.getSelectedRoute(MediaRouter.ROUTE_TYPE_LIVE_VIDEO)

route?.let {
    val presentation = StreamPresentation(this, it.presentationDisplay)
    presentation.show()
}
```

#### D. Complete Android App Structure

```
app/
├── MainActivity.kt          # Main activity, handles display selection
├── StreamReceiver.kt        # UDP/HTTP network receiving
├── VideoDecoder.kt          # MediaCodec decoding
├── StreamPresentation.kt    # Presentation for external display
└── StreamService.kt         # Optional: Background service
```

**Key Features:**
- Network stream receiving (UDP/RTP or HTTP MJPEG)
- Hardware-accelerated decoding (MediaCodec)
- External display output (Presentation API)
- Low latency configuration (minimal buffering)
- Connection management UI

---

### Option 2: Existing Android Apps (Not Recommended for Ultra-Low Latency)

#### A. VLC for Android

**Pros:**
- ✅ Already exists, no development
- ✅ Supports network streaming (RTP, HTTP)
- ✅ Hardware acceleration
- ✅ Can cast to external display

**Cons:**
- ❌ Buffering for smoothness (not ultra-low latency)
- ❌ May not achieve <50ms latency
- ❌ Less control over latency settings

**Usage:**
- Open VLC
- Menu → Stream → Network stream
- Enter URL: `rtp://@SERVER_IP:5000` or `http://SERVER_IP:8080/stream.mjpg`
- Cast to external display if available

#### B. MX Player

**Pros:**
- ✅ Network streaming support
- ✅ Hardware acceleration
- ✅ Popular app

**Cons:**
- ❌ Designed for media playback (buffering)
- ❌ Not optimized for low latency

#### C. Kodi

**Pros:**
- ✅ Media center, supports network streams
- ✅ Can output to external display

**Cons:**
- ❌ Heavy application
- ❌ Not optimized for low latency
- ❌ Overkill for simple streaming

#### D. Scrcpy (Not Applicable)

**Note**: Scrcpy is designed for Android→PC streaming, not PC→Android. The reverse direction would require significant modification.

---

## Recommendation

### For Raspberry Pi: Custom GStreamer Client

**Why:**
- ✅ Easy to implement (pipeline-based)
- ✅ Hardware acceleration (VideoCore GPU)
- ✅ Low latency achievable
- ✅ Flexible (can tune for performance)

**Implementation:**
- Use GStreamer C API or Python bindings
- UDP receiver → decoder → display sink
- Add simple UI for connection management (optional)

### For Android: Custom App with MediaCodec

**Why:**
- ✅ Ultra-low latency achievable (<50ms with MJPEG)
- ✅ Hardware acceleration (MediaCodec)
- ✅ Full control over buffering/latency
- ✅ DisplayPort output via Presentation API
- ⚠️ Requires Android development

**Implementation:**
- Kotlin/Java Android app
- UDP socket for receiving
- MediaCodec for hardware decoding
- Presentation API for DisplayPort output
- Minimal buffering for low latency

**Alternative (Quick Test):**
- Use VLC for Android initially to test streaming
- If latency is acceptable, use VLC
- If not, develop custom app for lower latency

---

## Comparison

| Solution | Latency | Development Time | Hardware Accel | DisplayPort |
|----------|---------|------------------|----------------|-------------|
| **Raspberry Pi: GStreamer** | <50ms (MJPEG) | Low | ✅ Yes | ✅ HDMI/DP |
| **Raspberry Pi: VLC** | 100-200ms | None | ✅ Yes | ✅ HDMI/DP |
| **Android: Custom App** | <50ms (MJPEG) | Medium | ✅ Yes | ✅ Yes |
| **Android: VLC** | 100-200ms | None | ✅ Yes | ✅ Yes |

**Best for ultra-low latency:**
- Raspberry Pi: Custom GStreamer client
- Android: Custom app with MediaCodec

**Best for quick testing:**
- Raspberry Pi: VLC
- Android: VLC for Android

