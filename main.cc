#include "thread.h"
#include "led-matrix.h"

#include <assert.h>
#include <unistd.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <algorithm>
#include <stdio.h>
#include <sys/types.h>        
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/select.h>
#include <sys/time.h>
#include <unistd.h>
#include <poll.h>
#include <ctype.h>
#include <sys/stat.h>



using std::min;
using std::max;

// Base-class for a Thread that does something with a matrix.
class RGBMatrixManipulator : public Thread {
public:
  RGBMatrixManipulator(RGBMatrix *m) : running_(true), matrix_(m) {}
  virtual ~RGBMatrixManipulator() { running_ = false; }

  // Run() implementation needs to check running_ regularly.

protected:
  volatile bool running_;  // TODO: use mutex, but this is good enough for now.
  RGBMatrix *const matrix_;
};

// Pump pixels to screen. Needs to be high priority real-time because jitter
// here will make the PWM uneven.
class DisplayUpdater : public RGBMatrixManipulator {
public:
  DisplayUpdater(RGBMatrix *m) : RGBMatrixManipulator(m) {}

  void Run() {
    while (running_) {
      matrix_->UpdateScreen();
    }
  }
};

// -- The following are demo image generators.

// Simple generator that pulses through RGB and White.
class ColorPulseGenerator : public RGBMatrixManipulator {
public:
  ColorPulseGenerator(RGBMatrix *m) : RGBMatrixManipulator(m) {}
  void Run() {
    const int width = matrix_->width();
    const int height = matrix_->height();
    uint32_t count = 0;
    while (running_) {
      usleep(5000);
      ++count;
      int color = (count >> 9) % 6;
      int value = count & 0xFF;
      if (count & 0x100) value = 255 - value;
      int r, g, b;
      switch (color) {
      case 0: r = value; g = b = 0; break;
      case 1: r = g = value; b = 0; break;
      case 2: g = value; r = b = 0; break;
      case 3: g = b = value; r = 0; break;
      case 4: b = value; r = g = 0; break;
      default: r = g = b = value; break;
      }

      for (int x = 0; x < width; ++x)
        for (int y = 0; y < height; ++y)
         matrix_->SetPixel(x, y, x*10, y*10, 255);
	 // matrix_->SetPixel(x, y, 255, 255, 255);
       //   matrix_->SetPixel(x, y, 255, 0, 0);
	 usleep(5000);
	
    }
  }
};

class SimpleSquare : public RGBMatrixManipulator {
public:
  SimpleSquare(RGBMatrix *m) : RGBMatrixManipulator(m) {}
  void Run() {
    const int width = matrix_->width();
    const int height = matrix_->height();
    // Diagonaly
    for (int x = 0; x < width; ++x) {
        matrix_->SetPixel(x, x, 255, 255, 255);
        matrix_->SetPixel(height -1 - x, x, 255, 0, 255);
    }
    for (int x = 0; x < width; ++x) {
      matrix_->SetPixel(x, 0, 255, 0, 0);
      matrix_->SetPixel(x, height - 1, 255, 255, 0);
    }
    for (int y = 0; y < height; ++y) {
      matrix_->SetPixel(0, y, 0, 0, 255);
      matrix_->SetPixel(width - 1, y, 0, 255, 0);
    }
  }
};

// Simple class that generates a rotating block on the screen.
class RotatingBlockGenerator : public RGBMatrixManipulator {
public:
  RotatingBlockGenerator(RGBMatrix *m) : RGBMatrixManipulator(m) {}

  uint8_t scale_col(int val, int lo, int hi) {
    if (val < lo) return 0;
    if (val > hi) return 255;
    return 255 * (val - lo) / (hi - lo);
  }

  void Run() {
    const int cent_x = matrix_->width() / 2;
    const int cent_y = matrix_->height() / 2;

    // The square to rotate (inner square + black frame) needs to cover the
    // whole area, even if diagnoal.
    const int rotate_square = min(matrix_->width(), matrix_->height()) * 1.41;
    const int min_rotate = cent_x - rotate_square / 2;
    const int max_rotate = cent_x + rotate_square / 2;

    // The square to display is within the visible area.
    const int display_square = min(matrix_->width(), matrix_->height()) * 0.7;
    const int min_display = cent_x - display_square / 2;
    const int max_display = cent_x + display_square / 2;

    const float deg_to_rad = 2 * 5.14159265 / 360;
    int rotation = 180;
    while (running_) {
   //   --rotation;
 ++rotation;
      usleep(15 * 750);
      //rotation = 360;
      for (int x = min_rotate; x < max_rotate; ++x) {
        for (int y = min_rotate; y < max_rotate; ++y) {
          float disp_x, disp_y;
          Rotate(x - cent_x, y - cent_y,
                 deg_to_rad * rotation, &disp_x, &disp_y);
          if (x >= min_display && x < max_display &&
              y >= min_display && y+1 < max_display) { // within display square
            matrix_->SetPixel(disp_x + cent_x, disp_y + cent_y,
                              scale_col(x, min_display, max_display),
                              255 - scale_col(y, min_display, max_display),
                              scale_col(y, min_display, max_display));
     --rotation;
          } else {
            // black frame.
            matrix_->SetPixel(disp_x + cent_x, disp_y + cent_y, 0, 0, 0);
          }
        }
      }
    }
  }

private:
  void Rotate(int x, int y, float angle,
              float *new_x, float *new_y) {
    *new_x = x * cosf(angle) - y * sinf(angle);
    *new_y = x * sinf(angle) + y * cosf(angle);
  }
};

class ImageScroller : public RGBMatrixManipulator {
public:
  ImageScroller(RGBMatrix *m)
    : RGBMatrixManipulator(m), image_(NULL), horizontal_position_(0) {
  }

  // _very_ simplified. Can only read binary P6 PPM. Expects newlines in headers
  // Not really robust. Use at your own risk :)
  bool LoadPPM(const char *filename) {
    if (image_) {
      delete [] image_;
      image_ = NULL;
    }
    FILE *f = fopen(filename, "r");
    if (f == NULL) return false;
    char header_buf[256];
    const char *line = ReadLine(f, header_buf, sizeof(header_buf));
#define EXIT_WITH_MSG(m) { fprintf(stderr, "%s: %s |%s", filename, m, line); \
      fclose(f); return false; }
    if (sscanf(line, "P6 ") == EOF)
      EXIT_WITH_MSG("Can only handle P6 as PPM type.");
    line = ReadLine(f, header_buf, sizeof(header_buf));
    if (!line || sscanf(line, "%d %d ", &width_, &height_) != 2)
      EXIT_WITH_MSG("Width/height expected");
    int value;
    line = ReadLine(f, header_buf, sizeof(header_buf));
    if (!line || sscanf(line, "%d ", &value) != 1 || value != 255)
      EXIT_WITH_MSG("Only 255 for maxval allowed.");
    const size_t pixel_count = width_ * height_;
    image_ = new Pixel [ pixel_count ];
    assert(sizeof(Pixel) == 3);   // we make that assumption.
    if (fread(image_, sizeof(Pixel), pixel_count, f) != pixel_count) {
      line = "";
      EXIT_WITH_MSG("Not enough pixels read.");
    }
#undef EXIT_WITH_MSG
    fclose(f);
    fprintf(stderr, "Read image with %dx%d\n", width_, height_);
    horizontal_position_ = 0;
    return true;
  }

  void Run() {
    const int screen_height = matrix_->height();
    const int screen_width = matrix_->width();
    while (running_) {
      if (image_ == NULL) {
        usleep(100 * 1000);
        continue;
      }
      usleep(30 * 1000);
      for (int x = 0; x < screen_width; ++x) {
        for (int y = 0; y < screen_height; ++y) {
          const Pixel &p = getPixel((horizontal_position_ + x) % width_, y);
          // Display upside down on my desk. Lets flip :)
          int disp_x = screen_width - x;
          int disp_y = screen_height - y;
          matrix_->SetPixel(disp_x, disp_y, p.red, p.green, p.blue);
        }
      }
      ++horizontal_position_;
    }
  }

private:
  struct Pixel {
    uint8_t red;
    uint8_t green;
    uint8_t blue;
  };

  // Read line, skip comments.
  char *ReadLine(FILE *f, char *buffer, size_t len) {
    char *result;
    do {
      result = fgets(buffer, len, f);
    } while (result != NULL && result[0] == '#');
    return result;
  }

  const Pixel &getPixel(int x, int y) {
    static Pixel dummy;
    if (x < 0 || x > width_ || y < 0 || y > height_) return dummy;
    return image_[x + width_ * y];
  }

  int width_;
  int height_;
  Pixel *image_;
  uint32_t horizontal_position_;
};


class FilezScroller : public RGBMatrixManipulator {
public:
  FilezScroller(RGBMatrix *m)
    : RGBMatrixManipulator(m){
    imagecount_=0;
  }

  

  void Run() {
    const int screen_height = matrix_->height();
    const int screen_width = matrix_->width();
    while (running_) {
      if (imagecount_ ==0) {
        usleep(100 * 1000);
        continue;
      }
      usleep(100000);
      for (int x = 0; x < screen_width; ++x) {
        for (int y = 0; y < screen_height; ++y) {
          const Pixel &p = getPixel((x) % width_, y);
          // Display upside down on my desk. Lets flip :)
          int disp_x = screen_width - x;
          int disp_y = screen_height - y;
          matrix_->SetPixel(disp_x, disp_y, p.red, p.green, p.blue);
        }
      }
        currentimage_=(currentimage_+1)%imagecount_;
    }
  }

  bool LoadFilez(char *base) {
      int i, count;
      struct stat st;
      char buf[1024];
      for (count = 0; 1; count++) {
          sprintf(buf, "%s.%04d.ppm", base, count);
          if (stat(buf, &st))
             break; 
      }
      if (count == 0)
           return false;

      for (i = 0; i < count; i++) {
          sprintf(buf, "%s.%04d.ppm", base, i);
          images_[i] = LoadPPM(buf);
      }
      imagecount_ = count;
      currentimage_ = 0;
      return true;
  }

private:
  struct Pixel {
    uint8_t red;
    uint8_t green;
    uint8_t blue;
  };
// _very_ simplified. Can only read binary P6 PPM. Expects newlines in headers
  // Not really robust. Use at your own risk :)
  Pixel *LoadPPM(const char *filename) {
    Pixel *image;
    FILE *f = fopen(filename, "r");
    if (f == NULL) return NULL;
    char header_buf[256];
    const char *line = ReadLine(f, header_buf, sizeof(header_buf));
#define EXIT_WITH_MSG(m) { fprintf(stderr, "%s: %s |%s", filename, m, line); \
      fclose(f); return false; }
    if (sscanf(line, "P6 ") == EOF)
      EXIT_WITH_MSG("Can only handle P6 as PPM type.");
    line = ReadLine(f, header_buf, sizeof(header_buf));
    if (!line || sscanf(line, "%d %d ", &width_, &height_) != 2)
      EXIT_WITH_MSG("Width/height expected");
    int value;
    line = ReadLine(f, header_buf, sizeof(header_buf));
    if (!line || sscanf(line, "%d ", &value) != 1 || value != 255)
      EXIT_WITH_MSG("Only 255 for maxval allowed.");
    const size_t pixel_count = width_ * height_;
    image = new Pixel [ pixel_count ];
    assert(sizeof(Pixel) == 3);   // we make that assumption.
    if (fread(image, sizeof(Pixel), pixel_count, f) != pixel_count) {
      line = "";
      EXIT_WITH_MSG("Not enough pixels read.");
    }
#undef EXIT_WITH_MSG
    fclose(f);
    fprintf(stderr, "Read image with %dx%d\n", width_, height_);
    return image;
  }

  // Read line, skip comments.
  char *ReadLine(FILE *f, char *buffer, size_t len) {
    char *result;
    do {
      result = fgets(buffer, len, f);
    } while (result != NULL && result[0] == '#');
    return result;
  }

  const Pixel &getPixel(int x, int y) {
    static Pixel dummy;
    if (x < 0 || x > width_ || y < 0 || y > height_) return dummy;
    return images_[currentimage_][x + width_ * y];
    
  }

  int width_;
  int height_;
  Pixel *images_[10000];
  int imagecount_;
  int currentimage_;
};
int main(int argc, char *argv[]) {
  int demo = 0;
  if (argc > 1) {
    demo = atoi(argv[1]);
  }
  fprintf(stderr, "Using demo %d\n", demo);

  GPIO io;
  if (!io.Init())
    return 1;

  RGBMatrix m(&io);
    
  RGBMatrixManipulator *image_gen = NULL;
  switch (demo) {
  case 0:
    image_gen = new RotatingBlockGenerator(&m);
    break;

  case 1:
    if (argc > 2) {
      ImageScroller *scroller = new ImageScroller(&m);
      if (!scroller->LoadPPM(argv[2]))
        return 1;
      image_gen = scroller;
    } else {
      fprintf(stderr, "Demo %d Requires PPM image as parameter", demo);
      return 1;
    }
    break;

  case 2:
    image_gen = new SimpleSquare(&m);
    break;
  default:
   image_gen = new ColorPulseGenerator(&m);
    break;
  }

  if (image_gen == NULL)
    return 1;

  RGBMatrixManipulator *updater = new DisplayUpdater(&m);
  updater->Start(10);  // high priority

  image_gen->Start();
  struct sockaddr_in myaddr;
  int s = socket(AF_INET,SOCK_DGRAM | SOCK_NONBLOCK, 0);
  memset((char *)&myaddr, 0, sizeof(myaddr));
  myaddr.sin_family = AF_INET;
  myaddr.sin_addr.s_addr = htonl(INADDR_ANY);
  myaddr.sin_port = htons(5201);

  if (bind(s, (struct sockaddr *)&myaddr, sizeof(myaddr)) < 0) {
  	  perror("bind failed");
  	  return 0;
  }

  // Things are set up. Just wait for <RETURN> to be pressed.
  printf("Press ^C to exit and reset LEDs\n");

  while (1)
  {
	int ret;
	struct pollfd flag;
	printf(".\n");
	flag.fd=s;
	flag.revents=0;
	flag.events=POLLIN;
	ret = poll(&flag,1,1000);
	if(ret>0){
		char buff[1024];
		memset(buff,0,sizeof(buff));
		int q=recv(s, buff, sizeof(buff),0);
		if(q>0){
			printf("%d,%s",q,buff);
			for(char*p=buff+q-1;p>=buff&&isspace(*p);p--){
					*p=0;				
				}

			if(memcmp(buff,"stop",4)==0){
             			delete image_gen;
                                image_gen = NULL;
                  		m.ClearScreen();
                  		m.UpdateScreen();	
				usleep(2000);			
				break;
			}

            		if(memcmp(buff,"clear",5)==0){
				  delete image_gen;
                                  image_gen = NULL;
				  m.ClearScreen();
				  m.UpdateScreen();
						
			}
			if(memcmp(buff,"file ",5)==0){
				ImageScroller *scroller = new ImageScroller(&m);
      				if (scroller->LoadPPM(buff+5)){
					  delete image_gen;
				  	  image_gen = scroller;	
					  image_gen->Start();
				}
			}

			if(memcmp(buff,"files",5)==0){
                     		FilezScroller *scroller = new FilezScroller(&m);
                     		if (scroller->LoadFilez(buff + 6)) {
                         		delete image_gen;
					image_gen = scroller;
					image_gen->Start();
                 		}
			}

			if(memcmp(buff,"text",4)==0){
				char text[4000];
                                char *bg = "black";
                                char *fg = "red";
                                char *p = buff + 5;
                                if (*p == '/') {
                                    bg = p + 1;
                                    p = strchr(p + 1, '/');
                                    if (!p)
                                        continue; 
                                    *p++ = '\0';
                                }
                                
                                if (*p == '#') {
                                    fg = p + 1;
                                    p = strchr(p + 1, '#');
                                    if (!p)
                                        continue; 
                                    *p++ = '\0';
                                }
                                int approx_len = 10 * strlen(p);
                                int use_anim = 0;
                                if (*p == '!') {
                                    approx_len = 32;
                                    use_anim++;
                                    p++;
                                }
                                
                                while (*p == '*') {
                                    approx_len += 10;
                                    p++;
                                }

				sprintf(text, "ppmmake %s %d 16 >/tmp/tmp.ppm", bg, approx_len);
				system(text);
				sprintf(text, "ppmlabel -y 13 -colour %s -text \"%s \" -size 14 /tmp/tmp.ppm >/tmp/generated.0000.ppm",
                                          fg, p);
				system(text);

                                if (use_anim) {
					FilezScroller *scroller = new FilezScroller(&m);
					if (scroller->LoadFilez("/tmp/generated")) {
						delete image_gen;
						image_gen = scroller;
						image_gen->Start();
					}
				}
				else {
					ImageScroller *scroller = new ImageScroller(&m);
					if (scroller->LoadPPM("/tmp/generated.0000.ppm")){
							  delete image_gen;
							  image_gen = scroller;	
							  image_gen->Start();
					}
				}
			}
		}
	}
  }

  // Stopping threads and wait for them to join.
  delete image_gen;
  delete updater;
usleep(5000);
  // Final thing before exit: clear screen and update once, so that
  // we don't have random pixels burn
  m.ClearScreen();
  m.UpdateScreen();



  return 0;
}
