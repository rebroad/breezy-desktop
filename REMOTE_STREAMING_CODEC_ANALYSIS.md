# Remote Streaming Codec and Protocol Analysis

## Question

What is the best codec to use for casting the desktop remotely with low-latency? How do the various codecs/protocols compare? Better to use UDP or TCP? How does ChromeCast work? Is there a "standard" for TVs? Could we use HP's ZCentral to cast the virtual framebuffer?

---

## Low-Latency Codec Comparison

### Codec Options

| Codec | Latency | Compression | Hardware Support | Use Cases |
|-------|---------|-------------|------------------|-----------|
| **H.264** | Medium | Good | Excellent (universal) | General streaming, compatibility |
| **H.265/HEVC** | Medium | Excellent | Good (newer devices) | High resolution, bandwidth-limited |
| **VP8** | Low-Medium | Medium | Good (WebRTC) | WebRTC, low-latency |
| **VP9** | Medium | Excellent | Good (ChromeCast, newer) | High quality, bandwidth-limited |
| **AV1** | Medium | Excellent | Limited (newest devices) | Future-proof, best compression |
| **MJPEG** | Very Low | Poor | Excellent | Ultra-low latency, local network |
| **Raw/Uncompressed** | Lowest | None | N/A | Zero latency, high bandwidth |

### Codec Characteristics for Low-Latency

**Ultra-Low Latency (<50ms) - Best for Local Network:**
- **MJPEG**: No temporal compression, each frame independent
  - **Latency**: ~17-20ms (lowest of all codecs)
  - **Bandwidth**: High (but acceptable on local network)
  - **Best choice for local network streaming**
- **Raw/Uncompressed**: No encoding overhead (minimal latency)
- Trade-off: High bandwidth requirements (fine for local network)

**Low Latency (50-100ms):**
- **VP8/VP9**: Designed for low-latency streaming (WebRTC)
- **H.264 (low latency profile)**: With tuned settings, can achieve <100ms (~19ms on local network)
- Trade-off: Some compression, but manageable latency

**Medium Latency (100-200ms):**
- **H.264 (standard)**: General purpose, good quality
- **H.265/HEVC**: Better compression, similar latency
- Trade-off: Better quality/compression, higher latency

**High Latency (>200ms):**
- **H.264/HEVC (high quality)**: Optimized for quality over latency
- Not suitable for interactive use

---

## UDP vs TCP for Streaming

### UDP (User Datagram Protocol)

**Advantages:**
- ✅ **Lower latency**: No retransmission delays
- ✅ **Lower overhead**: No connection setup, no ACK packets
- ✅ **Better for real-time**: Packet loss acceptable for video (frame drops better than stuttering)
- ✅ **Multicast support**: Can stream to multiple clients efficiently

**Disadvantages:**
- ❌ **No reliability**: Lost packets are not retransmitted
- ❌ **No ordering guarantee**: Packets may arrive out of order
- ❌ **Firewall issues**: Some networks block UDP
- ❌ **No congestion control**: Can overwhelm network if not handled

**Best for:**
- Real-time video streaming
- Low-latency applications
- Local network (low packet loss)

### TCP (Transmission Control Protocol)

**Advantages:**
- ✅ **Reliability**: Lost packets are retransmitted
- ✅ **Ordering**: Packets arrive in order
- ✅ **Congestion control**: Adapts to network conditions
- ✅ **Firewall friendly**: Usually allowed through firewalls
- ✅ **Error correction**: Guaranteed delivery

**Disadvantages:**
- ❌ **Higher latency**: Retransmission delays can be significant
- ❌ **Head-of-line blocking**: One lost packet delays all subsequent packets
- ❌ **Higher overhead**: Connection setup, ACKs, flow control
- ❌ **Not ideal for real-time**: Retransmissions cause stuttering

**Best for:**
- File transfers
- High reliability requirements
- High packet loss networks (WAN)

### Hybrid Approach (Recommended)

**RTP over UDP** (Real-time Transport Protocol):
- Uses UDP for transport (low latency)
- Adds sequence numbers and timestamps (RTP header)
- Application-level error handling for critical data
- Allows frame drops for video (acceptable quality degradation)

**Example:** WebRTC uses RTP over UDP for video, with RTCP for control/synchronization.

**Recommendation for Remote Desktop Streaming:**
- ✅ **Use UDP with RTP** for video frames (low latency, tolerate packet loss)
- ✅ **Use TCP** for control/data (xrandr commands, configuration)
- ✅ **Use adaptive bitrate** to handle network conditions

---

## ChromeCast Protocol Analysis

### How ChromeCast Works

**Protocol Stack:**
1. **Discovery**: mDNS/Bonjour (local network discovery)
2. **Connection**: HTTP/HTTPS (initial connection and control)
3. **Streaming**:
   - **WebRTC** (low-latency screen casting)
   - **MPEG-DASH/HLS** (media streaming)
   - **H.264/VP8/VP9** (codecs)

**Architecture:**
```
Source Device → ChromeCast SDK → Google Cast Protocol → ChromeCast Device
                     ↓
            Encodes video (H.264/VP8/VP9)
                     ↓
            Streams via WebRTC or DASH
```

**Codecs:**
- **VP8/VP9**: Primary codecs (ChromeCast optimized)
- **H.264**: Fallback for compatibility
- **AV1**: Newer ChromeCast devices

**Latency:**
- **Mirroring (screen cast)**: ~100-200ms (WebRTC)
- **Media playback**: Variable (buffering for quality)
- **Optimized for**: Quality over ultra-low latency

**Key Features:**
- Hardware acceleration on ChromeCast device
- Adaptive bitrate (adjusts to network)
- Frame rate throttling (reduces bandwidth when static)
- Audio/video synchronization

### ChromeCast for Our Use Case

**Could we use ChromeCast?**
- ⚠️ **Protocol**: Requires Google Cast SDK (proprietary, but open protocol)
- ⚠️ **Codec support**: VP8/VP9/H.264 (compatible)
- ⚠️ **Latency**: 100-200ms (may be acceptable for desktop use)
- ✅ **Hardware**: ChromeCast devices are common and affordable
- ❌ **Integration**: Would need to implement Cast SDK or compatible protocol
- ❌ **Device limitation**: Only works with ChromeCast devices

**Verdict**: Possible but not ideal - proprietary protocol, limited device support.

---

## TV Casting Standards

### Miracast (Wi-Fi Alliance)

**Standard**: Wi-Fi Alliance standard for screen mirroring
**Protocol**: Wi-Fi Direct (peer-to-peer Wi-Fi)
**Codec**: H.264 (mandatory)
**Latency**: 50-150ms (low latency optimized)

**Features:**
- Direct Wi-Fi connection (no router needed)
- Hardware acceleration (most TVs support)
- Low latency (designed for screen mirroring)
- Wide device support (Android, Windows, some TVs)

**Pros:**
- ✅ Industry standard (not proprietary)
- ✅ Low latency
- ✅ Wide device support
- ✅ Direct connection (no network infrastructure)

**Cons:**
- ❌ Requires Wi-Fi Direct support
- ❌ H.264 only (no VP8/VP9)
- ❌ Connection setup can be finicky
- ❌ Not all devices support it

### DLNA (Digital Living Network Alliance)

**Standard**: DLNA guidelines for media sharing
**Protocol**: HTTP (over TCP)
**Codec**: Various (H.264, MPEG-2, etc.)
**Latency**: High (not designed for real-time)

**Features:**
- Media sharing (not screen casting)
- File-based streaming
- High latency (buffering)

**Verdict**: Not suitable for low-latency desktop streaming.

### AirPlay (Apple)

**Standard**: Apple proprietary protocol
**Protocol**: RTSP/RTP over UDP
**Codec**: H.264, H.265
**Latency**: ~100-200ms

**Features:**
- Apple ecosystem integration
- Low latency (optimized for screen mirroring)
- Hardware acceleration
- Encrypted streaming

**Verdict**: Only works with Apple devices (not suitable for Linux).

### WebRTC (Web Real-Time Communication)

**Standard**: W3C/IETF standard
**Protocol**: RTP/SRTP over UDP
**Codec**: VP8, VP9, H.264
**Latency**: 50-150ms (ultra-low latency)

**Features:**
- Open standard
- Ultra-low latency
- Adaptive bitrate
- Built-in encryption (SRTP)
- NAT traversal (ICE/STUN/TURN)

**Verdict**: Best option for low-latency streaming - open standard, low latency, wide support.

---

## HP ZCentral Remote Boost

### What is ZCentral Remote Boost?

HP ZCentral Remote Boost is a remote desktop solution that allows remote access to HP Z workstations with high-performance graphics.

**Protocol**: Proprietary (likely based on RDP or similar)
**Codec**: Likely H.264 or proprietary
**Latency**: Optimized for remote work (may not be ultra-low latency)

**Features:**
- GPU acceleration
- Multi-display support
- Optimized for CAD/graphics workloads
- Windows-focused

### Could We Use ZCentral?

**For casting virtual framebuffer:**
- ❌ **Proprietary**: Closed protocol, not open source
- ❌ **Workstation-focused**: Designed for remote HP workstations, not virtual displays
- ❌ **Windows-centric**: Likely requires Windows on both ends
- ❌ **Not designed for this use case**: Optimized for remote desktop, not display streaming
- ❌ **No Linux support**: Unlikely to work on Linux

**Verdict**: Not applicable - proprietary, Windows-only, not designed for virtual display streaming.

---

## Recommendations for Remote Virtual Display Streaming

### Best Approach: WebRTC with VP8/H.264

**Why WebRTC?**
1. ✅ **Open standard**: W3C/IETF standard, not proprietary
2. ✅ **Ultra-low latency**: Designed for real-time communication (50-150ms)
3. ✅ **UDP-based**: RTP/SRTP over UDP (low latency)
4. ✅ **Adaptive bitrate**: Adjusts to network conditions
5. ✅ **Wide support**: Browsers, mobile apps, can be embedded
6. ✅ **Encryption**: Built-in SRTP encryption
7. ✅ **NAT traversal**: ICE/STUN/TURN (works through firewalls)
8. ✅ **Multiple codecs**: VP8, VP9, H.264 support

**Architecture:**
```
Virtual Display (XR-Manager) → Capture (DMA-BUF) → Encode (VP8/H.264) → WebRTC → Remote Client
```

**Implementation Options:**

1. **GStreamer + WebRTC**:
   - GStreamer can capture virtual display (via PipeWire or direct)
   - `webrtcbin` element provides WebRTC stack
   - Supports VP8, VP9, H.264
   - Low latency configuration available

2. **libwebrtc (Google's WebRTC library)**:
   - Full control over WebRTC stack
   - More complex integration
   - Best performance/control

3. **Janus WebRTC Server** (if server-based approach needed):
   - WebRTC media server
   - Can be used as relay/server
   - More complex setup

### Alternative: RTP over UDP with H.264/VP8

**For simpler implementation:**
- Use GStreamer or FFmpeg for encoding
- Stream via RTP over UDP
- Simpler than full WebRTC stack
- Still low latency
- Need to handle signaling separately (if needed)

### Codec Selection

**For local network (lowest latency):**
- **MJPEG**: **Lowest latency (~17-20ms)**, each frame independent, no temporal compression
  - High bandwidth but acceptable on local network (Wi-Fi/Ethernet)
  - Simple encoding/decoding (fast CPU implementation)
  - **Best choice for local network streaming**
- **H.264 (low latency profile)**: ~19ms latency, better compression, universal hardware support
- **VP8**: ~50-100ms latency, good compression, WebRTC optimized

**For WAN/Internet (bandwidth-limited):**
- **VP8**: Best balance (low latency, good compression, wide support)
- **H.264 (low latency profile)**: Universal support, acceptable latency

**For quality over latency:**
- **VP9**: Better compression than VP8
- **H.265**: Best compression, but higher latency

**Recommendation: Start with VP8 or H.264 (low latency profile)**

---

## Protocol Stack Recommendation

### For Low-Latency Desktop Streaming:

```
┌─────────────────────────────────────────┐
│ Virtual Display (XR-Manager)            │
│  - Captured via DMA-BUF (zero-copy)     │
└──────────────┬──────────────────────────┘
               │
┌──────────────▼──────────────────────────┐
│ Video Encoder (VP8/H.264 low-latency)   │
│  - Hardware accelerated if available     │
│  - Tuned for low latency (<50ms encode) │
└──────────────┬──────────────────────────┘
               │
┌──────────────▼──────────────────────────┐
│ WebRTC Stack                             │
│  - RTP/SRTP over UDP (low latency)      │
│  - Adaptive bitrate                      │
│  - NAT traversal (ICE/STUN/TURN)        │
│  - Encryption (SRTP)                     │
└──────────────┬──────────────────────────┘
               │
┌──────────────▼──────────────────────────┐
│ Control Channel (TCP)                    │
│  - xrandr commands                       │
│  - Configuration                         │
│  - Synchronization                       │
└──────────────┬──────────────────────────┘
               │
┌──────────────▼──────────────────────────┐
│ Network (UDP for video, TCP for control)│
└─────────────────────────────────────────┘
```

### Implementation Stack:

1. **Capture**: DMA-BUF from virtual display framebuffer (already implemented)
2. **Encode**: GStreamer `x264enc` or `vp8enc` (low latency tuning)
3. **Stream**: GStreamer `webrtcbin` or RTP over UDP
4. **Control**: Simple TCP socket for xrandr commands/config

---

## Codec Tuning for Low Latency

### VP8 (Recommended)

**GStreamer pipeline example:**
```bash
gst-launch-1.0 \
  v4l2src ! \
  video/x-raw,format=NV12 ! \
  vp8enc target-bitrate=5000000 deadline=1 threads=4 ! \
  rtpvp8pay ! \
  udpsink host=REMOTE_IP port=5000
```

**Key parameters:**
- `deadline=1`: Low latency mode (1ms deadline)
- `target-bitrate`: Adjust based on network
- `threads=4`: Parallel encoding (reduces latency)

### H.264 (Low Latency Profile)

**GStreamer pipeline example:**
```bash
gst-launch-1.0 \
  v4l2src ! \
  video/x-raw,format=NV12 ! \
  x264enc \
    speed-preset=ultrafast \
    tune=zerolatency \
    bitrate=5000 \
    key-int-max=30 \
    threads=4 ! \
  rtph264pay ! \
  udpsink host=REMOTE_IP port=5000
```

**Key parameters:**
- `speed-preset=ultrafast`: Fastest encoding (lower quality, lower latency)
- `tune=zerolatency`: Zero latency tuning (minimal buffering)
- `key-int-max=30`: Keyframe interval (affects latency)
- `bitrate=5000`: Bitrate in kbps

---

## Client Options for Remote Display

### Raspberry Pi Client

**Options:**
1. **Custom Client Application**:
   - Linux-based, can run standard video player/streaming client
   - Can use GStreamer for decoding/display
   - Output to HDMI/DisplayPort connected display
   - Full control over implementation

2. **Existing Solutions**:
   - **VLC**: Supports MJPEG/H.264 streaming via RTP/UDP
   - **GStreamer pipeline**: Can decode and display video stream
   - **MPV**: Lightweight video player, supports network streaming

**Implementation Example (GStreamer on Raspberry Pi):**
```bash
# MJPEG stream receiver
gst-launch-1.0 \
  udpsrc port=5000 ! \
  image/jpeg,width=1920,height=1080,framerate=60/1 ! \
  jpegdec ! \
  videoconvert ! \
  waylandsink  # or xvimagesink for X11
```

### Android Client with DisplayPort Output

**Options:**
1. **Custom Android App**:
   - Native Android app using MediaCodec (hardware-accelerated decoding)
   - MJPEG/H.264/VP8 decoder
   - Display to external display via DisplayPort/HDMI (USB-C to DisplayPort)
   - Can use Android's Presentation API for second screen

2. **Existing Android Apps** (Screen Mirroring Receivers):
   - **VLC for Android**: Supports network streaming (RTP, HTTP)
   - **MX Player**: Network streaming support
   - **Kodi**: Media center, supports network streams
   - **Scrcpy (reverse)**: Typically for Android→PC, but architecture could be adapted

**Android Implementation Approach:**
- Use **MediaCodec API** for hardware-accelerated decoding (MJPEG/H.264)
- Use **Presentation API** or **MediaRouter** for DisplayPort output
- Network receiving via **HttpURLConnection** or native socket (UDP)
- Real-time playback using **SurfaceView** or **TextureView**

**Note**: Most existing Android apps are designed for media playback (with buffering), not ultra-low latency streaming. A custom app would be better for <50ms latency requirements.

### Recommendation

**For local network streaming:**
- **MJPEG** for lowest latency (~17-20ms)
- **Raspberry Pi**: Custom client using GStreamer (easiest) or VLC
- **Android**: Custom app using MediaCodec (MJPEG/H.264) + Presentation API for DisplayPort output
- Existing apps may work but won't achieve ultra-low latency (they buffer for smoothness)

---

## Summary and Recommendations

### Best Solution for Local Network: MJPEG over UDP

**For local network (your use case):**
- **Codec**: **MJPEG** (lowest latency: ~17-20ms)
- **Protocol**: RTP over UDP (or simple UDP)
- **Bandwidth**: High but acceptable on local Wi-Fi/Ethernet
- **Implementation**: Simple (no complex encoding/decoding)

### Alternative: WebRTC with VP8/H.264 over UDP (for WAN/Internet)

**Codec**: VP8 or H.264 (low latency profile)
**Protocol**: RTP/SRTP over UDP (via WebRTC)
**Control**: TCP for xrandr commands

**Why:**
1. ✅ Ultra-low latency (50-150ms achievable)
2. ✅ Open standard (not proprietary)
3. ✅ Wide device support (browsers, mobile, embedded)
4. ✅ Adaptive bitrate (handles network conditions)
5. ✅ Encryption built-in (SRTP)
6. ✅ NAT traversal (works through firewalls)

### Implementation Path

1. **Phase 1**: Simple RTP/UDP streaming
   - Use GStreamer for capture/encode/stream
   - VP8 or H.264 encoder (low latency tuned)
   - RTP over UDP
   - Test latency and quality

2. **Phase 2**: Add WebRTC (if needed)
   - Integrate WebRTC stack for better NAT traversal
   - Add adaptive bitrate
   - Add encryption

3. **Phase 3**: xrandr proxying
   - TCP socket for control channel
   - Translate remote xrandr commands to XR-Manager
   - Synchronize display configuration

### Comparison Table

| Solution | Latency | Complexity | Compatibility | Open Source |
|----------|---------|------------|---------------|-------------|
| **WebRTC (VP8)** | 50-150ms | Medium | Excellent | ✅ Yes |
| **RTP/UDP (H.264)** | 50-150ms | Low | Excellent | ✅ Yes |
| **Miracast** | 50-150ms | Medium | Good | ⚠️ Partial |
| **ChromeCast** | 100-200ms | High | Limited | ❌ No |
| **HP ZCentral** | Unknown | High | Limited | ❌ No |
| **DLNA** | High | Low | Good | ✅ Yes |
| **AirPlay** | 100-200ms | Medium | Limited | ❌ No |

**Winner: WebRTC with VP8** - Best balance of latency, compatibility, and open standards.

