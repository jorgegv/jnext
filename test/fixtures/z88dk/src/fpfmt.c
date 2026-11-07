/* GH #477: float objects of every shape, no float arithmetic (so every float
   format links, mbf40 included, which has no library). Never name an object
   `fa`: genmath and the ROM maths define their own. */
float f0 = 0.0, f1 = -3.25, f2 = 0.1, f3 = 12345.678, f4 = 1e-5;
float farr[3] = { 1.5, -2.0, 100.0 };
float *pf = farr;
struct fs { float v; int n; } gs = { 2.5, 7 };
int main(void) { return 0; }
