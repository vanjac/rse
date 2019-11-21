void bmp8_plot(int x, int y, u32 clr, void *dstBase, u32 dstP);

void bmp8_hline(int x1, int y, int x2, u32 clr, void *dstBase, u32 dstP);
void bmp8_vline(int x, int y1, int y2, u32 clr, void *dstBase, u32 dstP);
void bmp8_line(int x1, int y1, int x2, int y2, u32 clr, 
	void *dstBase, u32 dstP);

void bmp8_rect(int left, int top, int right, int bottom, u32 clr,
	void *dstBase, u32 dstP);
void bmp8_frame(int left, int top, int right, int bottom, u32 clr,
	void *dstBase, u32 dstP);
