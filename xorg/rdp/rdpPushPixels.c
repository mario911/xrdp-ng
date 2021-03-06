/*
Copyright 2005-2012 Jay Sorg

Permission to use, copy, modify, distribute, and sell this software and its
documentation for any purpose is hereby granted without fee, provided that
the above copyright notice appear in all copies and that both that
copyright notice and this permission notice appear in supporting
documentation.

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
OPEN GROUP BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

 */

#include "rdp.h"
#include "rdpdraw.h"

#define LDEBUG 0

#define LOG_LEVEL 1
#define LLOG(_level, _args) \
		do { if (_level < LOG_LEVEL) { ErrorF _args ; } } while (0)
#define LLOGLN(_level, _args) \
		do { if (_level < LOG_LEVEL) { ErrorF _args ; ErrorF("\n"); } } while (0)

extern rdpScreenInfoRec g_rdpScreen; /* from rdpmain.c */
extern DevPrivateKeyRec g_rdpGCIndex; /* from rdpmain.c */
extern DevPrivateKeyRec g_rdpWindowIndex; /* from rdpmain.c */
extern DevPrivateKeyRec g_rdpPixmapIndex; /* from rdpmain.c */
extern int g_Bpp; /* from rdpmain.c */
extern ScreenPtr g_pScreen; /* from rdpmain.c */
extern Bool g_wrapPixmap; /* from rdpmain.c */
extern int g_do_dirty_os; /* in rdpmain.c */
extern int g_do_dirty_ons; /* in rdpmain.c */
extern rdpPixmapRec g_screenPriv; /* in rdpmain.c */

extern GCOps g_rdpGCOps; /* from rdpdraw.c */

extern int g_con_number; /* in rdpup.c */

/******************************************************************************/
void rdpPushPixelsOrg(GCPtr pGC, PixmapPtr pBitMap, DrawablePtr pDst,
		int w, int h, int x, int y)
{
	rdpGCPtr priv;
	GCFuncs *oldFuncs;

	GC_OP_PROLOGUE(pGC);
	pGC->ops->PushPixels(pGC, pBitMap, pDst, w, h, x, y);
	GC_OP_EPILOGUE(pGC);
}

/******************************************************************************/
void rdpPushPixels(GCPtr pGC, PixmapPtr pBitMap, DrawablePtr pDst,
		int w, int h, int x, int y)
{
	RegionRec clip_reg;
	RegionRec box_reg;
	RegionRec reg1;
	int num_clips;
	int cd;
	int j;
	int got_id;
	int dirty_type;
	int post_process;
	int reset_surface;
	BoxRec box;
	struct image_data id;
	WindowPtr pDstWnd;
	PixmapPtr pDstPixmap;
	rdpPixmapRec *pDstPriv;
	rdpPixmapRec *pDirtyPriv;

	LLOGLN(10, ("rdpPushPixels:"));

	/* do original call */
	rdpPushPixelsOrg(pGC, pBitMap, pDst, w, h, x, y);

	dirty_type = 0;
	pDirtyPriv = 0;
	post_process = 0;
	reset_surface = 0;
	got_id = 0;

	if (pDst->type == DRAWABLE_PIXMAP)
	{
		pDstPixmap = (PixmapPtr)pDst;
		pDstPriv = GETPIXPRIV(pDstPixmap);

		if (xrdp_is_os(pDstPixmap, pDstPriv))
		{
			post_process = 1;

			if (g_do_dirty_os)
			{
				LLOGLN(10, ("rdpPushPixels: gettig dirty"));
				pDstPriv->is_dirty = 1;
				pDirtyPriv = pDstPriv;
				dirty_type = RDI_IMGLY;
			}
			else
			{
				rdpup_switch_os_surface(pDstPriv->rdpindex);
				reset_surface = 1;
				rdpup_get_pixmap_image_rect(pDstPixmap, &id);
				got_id = 1;
			}
		}
	}
	else
	{
		if (pDst->type == DRAWABLE_WINDOW)
		{
			pDstWnd = (WindowPtr)pDst;

			if (pDstWnd->viewable)
			{
				post_process = 1;

				if (g_do_dirty_ons)
				{
					LLOGLN(0, ("rdpPushPixels: gettig dirty"));
					g_screenPriv.is_dirty = 1;
					pDirtyPriv = &g_screenPriv;
					dirty_type = RDI_IMGLL;
				}
				else
				{
					rdpup_get_screen_image_rect(&id);
					got_id = 1;
				}
			}
		}
	}

	if (!post_process)
	{
		return;
	}

	memset(&box, 0, sizeof(box));
	RegionInit(&clip_reg, NullBox, 0);
	cd = rdp_get_clip(&clip_reg, pDst, pGC);

	if (cd == 1)
	{
		if (dirty_type != 0)
		{
			box.x1 = pDst->x + x;
			box.y1 = pDst->y + y;
			box.x2 = box.x1 + w;
			box.y2 = box.y1 + h;
			RegionInit(&reg1, &box, 0);
			draw_item_add_img_region(pDirtyPriv, &reg1, GXcopy, dirty_type);
			RegionUninit(&reg1);
		}
		else if (got_id)
		{
			rdpup_begin_update();
			rdpup_send_area(0, pDst->x + x, pDst->y + y, w, h);
			rdpup_end_update();
		}
	}
	else if (cd == 2)
	{
		box.x1 = pDst->x + x;
		box.y1 = pDst->y + y;
		box.x2 = box.x1 + w;
		box.y2 = box.y1 + h;
		RegionInit(&box_reg, &box, 0);
		RegionIntersect(&clip_reg, &clip_reg, &box_reg);
		num_clips = REGION_NUM_RECTS(&clip_reg);

		if (num_clips > 0)
		{
			if (dirty_type != 0)
			{
				RegionInit(&reg1, &box, 0);
				draw_item_add_img_region(pDirtyPriv, &clip_reg, GXcopy, dirty_type);
				RegionUninit(&reg1);
			}
			else if (got_id)
			{
				rdpup_begin_update();

				for (j = num_clips - 1; j >= 0; j--)
				{
					box = REGION_RECTS(&clip_reg)[j];
					rdpup_send_area(0, box.x1, box.y1, box.x2 - box.x1, box.y2 - box.y1);
				}

				rdpup_end_update();
			}
		}

		RegionUninit(&box_reg);
	}

	RegionUninit(&clip_reg);

	if (reset_surface)
	{
		rdpup_switch_os_surface(-1);
	}
}
