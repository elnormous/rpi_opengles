#include <vector>

#include <stdio.h>
#include <bcm_host.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>

#include <fcntl.h>
#include <linux/input.h>

#include <glob.h>

EGLDisplay display;
EGLSurface surface;
EGLContext context;

#define DIV_ROUND_UP(n,d) (((n) + (d) - 1) / (d))
#define BITS_PER_LONG (8 * sizeof(long))
#define BITS_TO_LONGS(nr) DIV_ROUND_UP(nr, BITS_PER_LONG)

static inline int
bit_is_set(const unsigned long* array, int bit)
{
    return !!(array[bit / BITS_PER_LONG] & (1LL << (bit % BITS_PER_LONG)));
}

int main(int argc, char* argv[])
{
    bcm_host_init();

    // get an EGL display connection
    display = eglGetDisplay(EGL_DEFAULT_DISPLAY);

    if (!display)
    {
        printf("Failed to get display\n");
        return 1;
    }

    // initialize the EGL display connection
    //EGLBoolean result;

    if (!eglInitialize(display, NULL, NULL))
    {
        printf("Failed to initialize EGL\n");
        return 1;
    }

    // get an appropriate EGL frame buffer configuration
    static const EGLint attributeList[] =
    {
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_NONE
    };
    EGLConfig config;
    EGLint numConfig;

    if (!eglChooseConfig(display, attributeList, &config, 1, &numConfig))
    {
        printf("Failed to choose EGL config\n");
        return 1;
    }

    // get an appropriate EGL frame buffer configuration
    if (!eglBindAPI(EGL_OPENGL_ES_API))
    {
        printf("Failed to bind OpenGL ES API\n");
        return 1;
    }

    // create an EGL rendering context
    static const EGLint contextAttributes[] =
    {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE
    };
    context = eglCreateContext(display, config, EGL_NO_CONTEXT, contextAttributes);

    if (context == EGL_NO_CONTEXT)
    {
        printf("Failed to create EGL context\n");
        return 1;
    }

    // create an EGL window surface
    uint32_t screenWidth;
    uint32_t screenHeight;
    int32_t success = graphics_get_display_size(0, &screenWidth, &screenHeight);

    if (success == -1)
    {
        printf("Failed to get display size\n");
        return 1;
    }

    VC_RECT_T dstRect;
    dstRect.x = 0;
    dstRect.y = 0;
    dstRect.width = screenWidth;
    dstRect.height = screenHeight;

    VC_RECT_T srcRect;
    srcRect.x = 0;
    srcRect.y = 0;
    srcRect.width = screenWidth;
    srcRect.height = screenHeight;

    DISPMANX_DISPLAY_HANDLE_T dispmanDisplay = vc_dispmanx_display_open(0);
    DISPMANX_UPDATE_HANDLE_T dispmanUpdate = vc_dispmanx_update_start(0);

    DISPMANX_ELEMENT_HANDLE_T dispmanElement = vc_dispmanx_element_add(dispmanUpdate, dispmanDisplay,
                0, &dstRect, 0,
                &srcRect, DISPMANX_PROTECTION_NONE,
                0, 0, DISPMANX_NO_ROTATE);

    static EGL_DISPMANX_WINDOW_T nativewindow;
    nativewindow.element = dispmanElement;
    nativewindow.width = screenWidth;
    nativewindow.height = screenHeight;
    vc_dispmanx_update_submit_sync(dispmanUpdate);

    surface = eglCreateWindowSurface(display, config, &nativewindow, NULL);
    if (surface == EGL_NO_SURFACE)
    {
        printf("Failed to create EGL window surface\n");
        return 1;
    }

    // connect the context to the surface
    if (!eglMakeCurrent(display, surface, surface, context))
    {
        printf("Failed to set current EGL context\n");
        return 1;
    }

    // input
    struct InputDeviceRPI
    {
        enum DeviceClass
        {
            CLASS_KEYBOARD = 1,
            CLASS_MOUSE = 2,
            CLASS_TOUCHPAD = 4,
            CLASS_GAMEPAD = 8
        };

        uint32_t deviceClass = 0;
        int fd = 0;
    };

    int maxFd = 0;
    std::vector<InputDeviceRPI> inputDevices;
    uint32_t mouseX = 0;
    uint32_t mouseY = 0;
    char TEMP[256];

    glob_t g;
    int result = glob("/dev/input/event*", GLOB_NOSORT, NULL, &g);

    if (result == GLOB_NOMATCH)
    {
        printf("No event devices found\n");
        return 1;
    }
    else if (result)
    {
        printf("Could not read /dev/input/event*\n");
        return 1;
    }

    for (size_t i = 0; i < g.gl_pathc; i++)
    {
        InputDeviceRPI inputDevice;

        inputDevice.fd = open(g.gl_pathv[i], O_RDONLY);
        if (inputDevice.fd == -1)
        {
            printf("Failed to open device file descriptor\n");
            continue;
        }

        if (ioctl(inputDevice.fd, EVIOCGRAB, (void *)1) == -1)
        {
            printf("Failed to get grab device\n");
        }

        memset(TEMP, 0, sizeof(TEMP));
        if (ioctl(inputDevice.fd, EVIOCGNAME(sizeof(TEMP) - 1), TEMP) == -1)
        {
            printf("Failed to get device name\n");
        }
        else
        {
            printf("Got device: %s\n", TEMP);
        }

        unsigned long eventBits[BITS_TO_LONGS(EV_CNT)];
        unsigned long absBits[BITS_TO_LONGS(ABS_CNT)];
        unsigned long relBits[BITS_TO_LONGS(REL_CNT)];
        unsigned long keyBits[BITS_TO_LONGS(KEY_CNT)];

        if (ioctl(inputDevice.fd, EVIOCGBIT(0, sizeof(eventBits)), eventBits) == -1 ||
            ioctl(inputDevice.fd, EVIOCGBIT(EV_ABS, sizeof(absBits)), absBits) == -1 ||
            ioctl(inputDevice.fd, EVIOCGBIT(EV_REL, sizeof(relBits)), relBits) == -1 ||
            ioctl(inputDevice.fd, EVIOCGBIT(EV_KEY, sizeof(keyBits)), keyBits) == -1)
        {
            printf("Failed to get device event bits\n");
        }
        
        if (bit_is_set(eventBits, EV_KEY) && (
             bit_is_set(keyBits, KEY_1) ||
             bit_is_set(keyBits, KEY_2) ||
             bit_is_set(keyBits, KEY_3) ||
             bit_is_set(keyBits, KEY_4) ||
             bit_is_set(keyBits, KEY_5) ||
             bit_is_set(keyBits, KEY_6) ||
             bit_is_set(keyBits, KEY_7) ||
             bit_is_set(keyBits, KEY_8) ||
             bit_is_set(keyBits, KEY_9) ||
             bit_is_set(keyBits, KEY_0)
            ))
        {
            printf("Keyboard\n");
            inputDevice.deviceClass = InputDeviceRPI::CLASS_KEYBOARD;
        }
        
        if (bit_is_set(eventBits, EV_ABS) && bit_is_set(absBits, ABS_X) && bit_is_set(absBits, ABS_Y))
        {
            if (bit_is_set(keyBits, BTN_STYLUS) || bit_is_set(keyBits, BTN_TOOL_PEN))
            {
                printf("Tablet\n");
                inputDevice.deviceClass |= InputDeviceRPI::CLASS_TOUCHPAD;
            } 
            else if (bit_is_set(keyBits, BTN_TOOL_FINGER) && !bit_is_set(keyBits, BTN_TOOL_PEN))
            {
                printf("Touchpad\n");
                inputDevice.deviceClass |= InputDeviceRPI::CLASS_TOUCHPAD;
            }
            else if (bit_is_set(keyBits, BTN_MOUSE))
            {
                printf("Mouse\n");
                inputDevice.deviceClass |= InputDeviceRPI::CLASS_MOUSE;
            }
            else if (bit_is_set(keyBits, BTN_TOUCH))
            {
                printf("Touchscreen\n");
                inputDevice.deviceClass |= InputDeviceRPI::CLASS_TOUCHPAD;
            }
        }
        else if (bit_is_set(eventBits, EV_REL) && bit_is_set(relBits, REL_X) && bit_is_set(relBits, REL_Y))
        {
            if (bit_is_set(keyBits, BTN_MOUSE))
            {
                printf("Mouse\n");
                inputDevice.deviceClass |= InputDeviceRPI::CLASS_MOUSE;
            }
        }

        if (bit_is_set(keyBits, BTN_JOYSTICK))
        {
            printf("Joystick\n");
            inputDevice.deviceClass = InputDeviceRPI::CLASS_GAMEPAD;
        }

        if (bit_is_set(keyBits, BTN_GAMEPAD))
        {
            printf("Gamepad\n");
            inputDevice.deviceClass = InputDeviceRPI::CLASS_GAMEPAD;
        }

        if (inputDevice.fd > maxFd)
        {
            maxFd = inputDevice.fd;
        }

        inputDevices.push_back(inputDevice);
    }

    globfree(&g);

    fd_set rfds;
    struct timeval tv;

    for(;;)
    {
        FD_ZERO(&rfds);

        for (const InputDeviceRPI& inputDevice : inputDevices)
        {
            FD_SET(inputDevice.fd, &rfds);
        }

        tv.tv_sec = 0;
        tv.tv_usec = 0;

        int retval = select(maxFd + 1, &rfds, NULL, NULL, &tv);

        if (retval == -1)
        {
            printf("Select failed\n");
            return 1;
        }
        else if (retval > 0)
        {
            for (const InputDeviceRPI& inputDevice : inputDevices)
            {
                if (FD_ISSET(inputDevice.fd, &rfds))
                {
                    ssize_t bytesRead = read(inputDevice.fd, TEMP, sizeof(TEMP));

                    if (bytesRead == -1)
                    {
                        printf("Failed to read input");
                    }

                    printf("Got input, read %d bytes\n", bytesRead);

                    for (ssize_t i = 0; i < bytesRead - static_cast<ssize_t>(sizeof(input_event)) + 1; i += sizeof(input_event))
                    {
                        input_event* event = reinterpret_cast<input_event*>(TEMP + i);

                        if (inputDevice.deviceClass & InputDeviceRPI::CLASS_KEYBOARD)
                        {
                            printf("Timestamp: %d.%d, type: %d", (uint32_t)event->time.tv_sec, (uint32_t)event->time.tv_usec, event->type);

                            switch (event->type)
                            {
                            case EV_SYN:
                                printf(", EV_SYN");
                                break;
                            case EV_KEY:
                                printf(", EV_KEY");
                                break;
                            case EV_MSC:
                                printf(", EV_MSC");
                                break;
                            case EV_REP:
                                printf(", EV_REP");
                                break;
                            }

                            printf(", value: %d, key: %d\n", event->value, event->code);

                            if (event->type == EV_KEY && event->code == KEY_ESC)
                            {
                                return 0;
                            }
                        }

                        if (inputDevice.deviceClass & InputDeviceRPI::CLASS_MOUSE)
                        {
                            printf("Timestamp: %d.%d, type: %d", (uint32_t)event->time.tv_sec, (uint32_t)event->time.tv_usec, event->type);

                            switch (event->type)
                            {
                            case EV_SYN:
                                printf(", EV_SYN");
                                break;
                            case EV_KEY:
                                printf(", EV_KEY");
                                break;
                            case EV_MSC:
                                printf(", EV_MSC");
                                break;
                            case EV_REL:
                                printf(", EV_REL");
                                break;
                            }

                            printf(", value: %d, key: %d\n", event->value, event->code);
                        }
                    }
                }
            }
        }

        glClearColor(1.0, 0.0, 0.0, 1.0);
        glClear(GL_COLOR_BUFFER_BIT);
        glFlush();

        eglSwapBuffers(display, surface);
    }

    for (const InputDeviceRPI& inputDevice : inputDevices)
    {
        if (ioctl(inputDevice.fd, EVIOCGRAB, (void*)0) == -1)
        {
            printf("Failed to release device\n");
        }

        if (close(inputDevice.fd) == -1)
        {
            printf("Failed to close file descriptor\n");
        }
    }

    if (!eglDestroySurface(display, surface))
    {
        printf("Failed to destroy EGL surface\n");
    }

    if (!eglDestroyContext(display, context))
    {
        printf("Failed to destroy EGL context\n");
    }

    if (!eglTerminate(display))
    {
        printf("Failed to terminate EGL\n");
    }

    bcm_host_deinit();

    return 0;
}
