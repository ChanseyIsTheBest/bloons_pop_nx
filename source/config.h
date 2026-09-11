/* ---------------------------------------------------------------------------
 * config.h -- build-time switches for the Bloons Pop Switch loader.
 * Game: com.ninjakiwi.bloonspuzzle 7.1 (1997), Unity 2020.3.15f2, IL2CPP arm64.
 * ------------------------------------------------------------------------- */
#ifndef BP_CONFIG_H
#define BP_CONFIG_H

/* ---- diagnostics --------------------------------------------------------
 * DEBUG_LOG 1 writes debug.log next to the .nro and enables every diagnostic
 * (module map, JNI ledger, patch/hook results, crash dumper). Keep it on for
 * the whole bring-up; it is the only instrument there is. */
#define DEBUG_LOG        0
#define DEBUG_JNI_TRACE  0
#define TRACE_IO         0
#define TRACE_MMAP       0

/* ---- game files (the folder name is resolved at runtime) --------------- */
#define BP_LIB_MAIN   "libmain.so"
#define BP_LIB_UNITY  "libunity.so"
#define BP_LIB_IL2CPP "libil2cpp.so"
#define BP_ASSET_DIR  "assets"
#define GAME_HOME     "sdmc:/switch/bloonspop"
#define LOG_NAME      "sdmc:/switch/bloonspop/debug.log"

/* ---- application identity (read from the APK: BuildConfig in classes2.dex)
 * Analytics/SDK init compare the package name against their own config, so
 * these are not cosmetic. 2020.3 ships no assets/bin/Data/unity_app_guid;
 * persistentDataPath derives from the package name instead. */
#define CS_PACKAGE       "com.ninjakiwi.bloonspuzzle"
#define CS_VERSION_NAME  "7.1"
#define CS_VERSION_CODE  1997
#define CS_APP_GUID      ""   /* 2020.3 ships no unity_app_guid; storage follows the package */

/* ---- presentation: PORTRAIT via Clay Jam's render-to-texture ------------
 * Bloons Pop is portrait-only (PlayerSettings: autorotate Portrait and
 * PortraitUpsideDown only, 768x1024 default). The Switch will not scan out a
 * portrait layer, so the engine renders into a 9:16 FBO and bp_tate.c rotates
 * it onto the landscape window before each swap; touch, stick, mouse and gyro
 * go through the inverse of the same rotation.
 *   BP_TATE_ROT 1 = 90 clockwise, 2 = 90 counter-clockwise. Hold the console
 *   so the picture is upright. BP_TATE_ENABLE 0 renders straight to the window
 *   (debugging only -- the image will be squashed). */
#define BP_TATE_ENABLE   1
#define BP_TATE_ROT      bp_portrait_rot   /* config.txt "portrait": 1 = CW, 2 = CCW */
#define BP_TATE_LINEAR   0            /* 0 = NEAREST (blit is 1:1 in handheld) */
/* The portrait resolution comes from config.txt at boot (bp_config.c): width =
 * the "p" number, 720..1080, height = width * 16 / 9. The swapchain is always
 * exactly the render size turned on its side, so the blit stays 1:1. The shape
 * rules the old _Static_asserts enforced are now guaranteed by the loader. */
extern int bp_res_w, bp_res_h, bp_portrait_rot;
#define BP_RENDER_W      bp_res_w     /* what the engine believes it has */
#define BP_RENDER_H      bp_res_h
#define BP_WINDOW_W      bp_res_h     /* the real swapchain, landscape */
#define BP_WINDOW_H      bp_res_w
#define BP_FORCE_SCREEN_W BP_RENDER_W
#define BP_FORCE_SCREEN_H BP_RENDER_H
extern int screen_width;               /* config.c; main.c assigns BP_FORCE_SCREEN_* */
extern int screen_height;

/* ---- rendering ------------------------------------------------------------
 * GLES via mesa/nouveau; libc_shim.c's dlopen refuse list keeps Vulkan off. */
#define BP_FORCE_GLES   1
#define BP_PATCH_VSYNC  1             /* start the vsync pump (bp_vsync.c) */
#define BP_VSYNC_PERIOD_NS 16666667ull
/* nativeRender()Z false for this many consecutive frames ends the loop. */
#define BP_RENDER_FALSE_EXIT_FRAMES 600u

/* ---- network (bp_net.c / bp_net_shim.c) ----------------------------------
 * Needed: 55 Addressables bundles stream from https://bundles.nkstatic.com/.
 * BP_NET_ENABLE 0 reports NotReachable and skips the CA install. */
#define BP_NET_ENABLE          1
#define BP_NET_BSD_SESSIONS    8
/* Socket-service buffer pool. libnx's default (4) gives ~2 MB of transfer memory;
 * each downloading TCP socket grows toward 256 KB of receive buffer, so a handful of
 * transfers exhaust it and new connections are refused (ninth hardware run:
 * "Cannot connect to destination host" partway through the bundle download). 8
 * doubles the pool; bp_net_init() falls back to the default config if refused. */
#define BP_NET_SB_EFFICIENCY   8
/* socketpair(): 0 = in-process pipe pair (default). Its only callers are curl's
 * resolver signal and multi wake-up pair -- both one-way, and a pipe costs the
 * socket service nothing. 1 = loopback TCP pair: two sockets + a listener per DNS
 * lookup, each leaving a TIME_WAIT entry; that churn is what failed first. */
#define BP_NET_SOCKETPAIR_LOOPBACK 0
/* Boot offline when every AssetBundle in bp_cache_manifest.h is already cached
 * (<root>/files/UnityCache/Shared/<id>/<hash>/). <root>/force_online overrides. */
#define BP_OFFLINE_WHEN_CACHED 1
#define BP_NET_REACH_CACHE_MS  1000

/* ---- audio (opensles.c + the FMOD pump) -------------------------------------
 * INHERITED FROM THE LINEAGE, UNTUNED FOR THIS GAME. If audio is silent with
 * "[audio] enq=0", see PORTING.md (FMOD buffer geometry). */
#define BP_AUDIO_DEVICE_RATE       24000
#define BP_AUDIO_FRAMES_PER_BUFFER 64
#define BP_AUDIO_CALLBACK_FRAMES   1024
#define BP_AUDIO_PERIOD_FRAMES     256
#define BP_AUDIO_UPFRONT_BUFFERS   4

/* ---- input ------------------------------------------------------------------
 * nx_pointer owns the touch panel, the stick/gyro cursor and USB mice; it is
 * the only input path (bp_input.c reports from it). Not optional. */
#define BP_ENABLE_POINTER_INPUT 1
#define BP_LOG_BUTTONS          0

/* ---- memory -----------------------------------------------------------------
 * MMAP_ARENA_ALIGN MUST equal BP_REGION_GRANULARITY_MB (bp_patch_granularity.h);
 * 64 MB is the lineage's known-good floor (16 MB corrupted the Dynamic Heap).
 * libunity 18 MB + libil2cpp 40 MB mapped here: far smaller than the lineage's
 * previous games, so the defaults leave plenty of headroom. */
#define MMAP_ARENA_ALIGN ((size_t)64 * 1024 * 1024)
#ifndef LOAD_ADDRESS
#define LOAD_ADDRESS 0xC0000000        /* inert; kept because the Makefile passes it */
#endif
#define BP_GPU_ARENA_MB 320
#define GFX_RESERVE_MB  192u

/* ---- inherited SDK knobs (Bouncemasters' EOS/loading-step tooling) ---------
 * Bloons Pop has no EOS; these keep inherited code compiling and inert. */
#define BP_EOS_INIT_RESULT         14
#define BP_FORCE_OFFLINE           0
#define BP_TRACE_LOADING_STEPS     0
#define BP_TRACE_REMOTECONFIG_BIND 0

/* ---------------------------------------------------------------------------
 * Portrait presentation ("TATE") -- adopted from clayjam_nx
 *
 * Bloons Pop is portrait-only (PlayerSettings: autorotate Portrait and
 * PortraitUpsideDown only, 768x1024 default). The Switch will not scan out a
 * portrait layer, so the engine renders into a 9:16 FBO and bp_tate.c rotates
 * it onto the landscape window before every swap. The inverse mapping for
 * touch and the cursor lives in the same file, so display and input cannot
 * disagree about which way round the screen is.
 *
 * BP_TATE_ROT: 1 = rotate 90 degrees clockwise, 2 = counter-clockwise. Hold
 * the console so the image is upright. Docked, the TV shows it sideways.
 * ------------------------------------------------------------------------ */
#define BP_TATE_ENABLE  1
#define BP_TATE_ROT      bp_portrait_rot   /* config.txt "portrait": 1 = CW, 2 = CCW */
#define BP_TATE_LINEAR  0     /* 0 = NEAREST; the blit is 1:1 in handheld */

/* BP_RENDER_* / BP_WINDOW_* are defined once above, from config.txt. */

/* ---------------------------------------------------------------------------
 * Networking -- adopted from acpc_nx
 *
 * Bloons Pop fetches content over HTTPS through UnityWebRequest (libcurl +
 * unitytls inside libunity). bp_net_shim.c is a real bionic-ABI socket layer;
 * bp_net.c brings up the BSD service and nifm and installs data/cacerts.pem
 * into unitytls' default CA list.
 *
 * BP_NET_BLOCK_ADS answers DNS for known ad/attribution/telemetry hosts with
 * "no such host". Their SDKs are Java-side and cannot run here anyway; this
 * keeps their native traffic from queueing behind the game's downloads.
 * ------------------------------------------------------------------------ */
#define BP_NET_BSD_SESSIONS 8
#define BP_NET_BLOCK_ADS    1
#define BP_NET_TRACE        0   /* log every connect/getaddrinfo */

/* Swappy is off in this game's PlayerSettings; the patches make sure. */
#define BP_PATCH_SWAPPY 1

/* ---------------------------------------------------------------------------
 * Engine clock (badpiggies_nx, re-derived for 2020.3.15f2) -- see bp_tmclock.c
 *
 * BP_TM_CLOCK         detour TimeManager::Update onto a monotonic clock
 * BP_TM_CLOCK_THREAD  keep the clock moving through synchronous scene loads
 * BP_TIME_ICALL_HOOKS 0 = hook the 11 Time icalls only if the TimeManager
 *                     clock could not be installed; 1 = always hook them.
 *                     With the engine clock live they are redundant, and the
 *                     engine's own Time values also reach Animator/particles.
 * ------------------------------------------------------------------------ */
#define BP_TM_CLOCK          1
#define BP_TM_CLOCK_THREAD   1
#define BP_TIME_ICALL_HOOKS  0

/* EnableFrameTimeTracker -> ret (the Looper/Monitor frame-2 deadlock) */
#define BP_PATCH_FRAMETIMETRACKER 1

/* FMOD OpenSL output: force the DSP period and up-front buffer count and defeat
 * the buffer-geometry bound check (bouncemasters_nx; identical code in 2020.3). */
#define BP_PATCH_FMOD 1

/* 0 = acpc_nx's blocking connect (proven). >0 = bounded non-blocking connect,
 * in milliseconds, for when a dead host would otherwise stall a download. */
#define BP_NET_CONNECT_TIMEOUT_MS 0

/* HTTPS certificate verification.
 * BP_TLS_VERIFY_DIAG        log refused chains (flags, subject/issuer per cert)
 *                           and save them as DER to <root>/tls/.
 * BP_TLS_INSECURE_FALLBACK  accept chains refused ONLY as NOT_TRUSTED (hostname
 *                           and dates still checked). Off by default: it lets
 *                           anyone on the network impersonate a server, and the
 *                           game sends Ninja Kiwi account credentials. Testing only. */
#define BP_TLS_VERIFY_DIAG       1
/* ON for this port (maintainer's decision): Bloons Pop only downloads game data here and
 * no login is used; the one server chain mbedtls refuses is otherwise unreachable. The
 * risk stands as described above -- a network attacker could feed the game altered data.
 * Set to 0 if a login path is ever enabled. */
#define BP_TLS_INSECURE_FALLBACK 1

/* JNI layer (daggerfall_nx, from clonehero_nx): LEDGER records every JNI call
 * the layer had to approximate (summarised in the log); QUARANTINE holds freed
 * local refs this deep before reuse, so a use-after-free reads a dead tag
 * instead of someone else's object. */
#define BP_JNI_LEDGER     1
#define BP_JNI_QUARANTINE 512
/* Third-party SDK Java classes (ads, stores, Kongregate, Facebook, ...) answer
 * null/0 instead of approximated objects: the shape of "SDK not installed". */
#define BP_JNI_SDK_NULL   1

#endif /* BP_CONFIG_H */
