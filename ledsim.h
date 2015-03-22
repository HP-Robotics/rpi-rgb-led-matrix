#define SIMULATOR           1

#ifdef __cplusplus
extern "C"{
#endif


/*----------------------------------------------------------------------------
**  Function prototypes; should work for ledsim.so and for led.so
**--------------------------------------------------------------------------*/
#if ! defined(LED_HANDLE_T)
#define LED_HANDLE_T void *
typedef void (*ledsim_x_callback)(LED_HANDLE_T h, unsigned long key);
#endif

LED_HANDLE_T led_init(void);
void led_get_size(LED_HANDLE_T h, int *wide, int *high);
void led_set_pixel(LED_HANDLE_T, int x, int y, int bright, int r, int g, int b);
void led_term(LED_HANDLE_T h);

/*  Function only used by the simulator to report X events */
#if defined(SIMULATOR)
void ledsim_set_x_callback(LED_HANDLE_T h, ledsim_x_callback x_callback);
void ledsim_join(LED_HANDLE_T h);
#endif

#ifdef __cplusplus
}
#endif

