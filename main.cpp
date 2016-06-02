#include <stdio.h>
#include <bcm_host.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>

#include <fcntl.h>
#include <linux/input.h>

EGLDisplay display;
EGLSurface surface;
EGLContext context;

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
    /*srcRect.width = screen_width << 16;
    srcRect.height = screen_height << 16;*/

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
    
    int keyboardFd = open("/dev/input/event0", O_RDONLY);
    
    if (keyboardFd == -1)
    {
        printf("No keyboard installed\n");
    }
    
    int mouseFd = open("/dev/input/event1", O_RDONLY);
    
    if (mouseFd == -1)
    {
        printf("No mouse installed\n");
    }
    
    fd_set rfds;
    struct timeval tv;
    int maxFd = keyboardFd;
    if (mouseFd > maxFd) maxFd = mouseFd;
    char TEMP[256];
    
    for(;;)
    {
        FD_ZERO(&rfds);
        FD_SET(keyboardFd, &rfds);
        FD_SET(mouseFd, &rfds);
        
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
            if (FD_ISSET(keyboardFd, &rfds))
            {
                ssize_t bytesRead = read(keyboardFd, TEMP, sizeof(TEMP));
                
                printf("Got input from keyboard, read %d bytes\n", bytesRead);
                
                struct input_event event;
                
                for (int i = 0; i < bytesRead - (int)sizeof(event) + 1; i += sizeof(event))
                {
                    memcpy(&event, TEMP +  i, sizeof(event));
                    
                    printf("Timestamp: %d.%d, type: %d", (uint32_t)event.time.tv_sec, (uint32_t)event.time.tv_usec, event.type);
                    
                    switch (event.type)
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
                    
                    printf(", value: %d, key: %d\n", event.value, event.code);
                }
            }
            else if (FD_ISSET(mouseFd, &rfds))
            {
                ssize_t bytesRead = read(mouseFd, TEMP, sizeof(TEMP));
                
                printf("Got input from mouse, read %d bytes\n", bytesRead);
                
                struct input_event event;
                
                for (int i = 0; i < bytesRead - (int)sizeof(event) + 1; i += sizeof(event))
                {
                    memcpy(&event, TEMP +  i, sizeof(event));
                    
                    printf("Timestamp: %d.%d, type: %d", (uint32_t)event.time.tv_sec, (uint32_t)event.time.tv_usec, event.type);
                    
                    switch (event.type)
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
                    
                    printf(", value: %d, key: %d\n", event.value, event.code);
                }
            }
        }
    
        glClearColor(1.0, 0.0, 0.0, 1.0);
        glClear(GL_COLOR_BUFFER_BIT);
        glFlush();

        eglSwapBuffers(display, surface);
    }
    
    close(mouseFd);
    close(keyboardFd);
    
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
