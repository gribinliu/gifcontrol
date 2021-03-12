// GifView.cpp : 实现文件
//

#include "stdafx.h"
#include "GifView.h"
//
#include "gif_lib.h"
#include <functional>
// CGifView
struct CGifView::Frame {
    const SavedImage* img;
    const ColorMapObject* color_map;
    GraphicsControlBlock gcb, last_gcb;
    //
    static Frame Parse(const SavedImage* img, const ColorMapObject* default_color_map, \
                       const GraphicsControlBlock& gcb, const GraphicsControlBlock& last_gcb)
    {
        Frame frame;
        frame.gcb = gcb;
        frame.color_map = img->ImageDesc.ColorMap ? img->ImageDesc.ColorMap : default_color_map;
        frame.img = img;
        frame.last_gcb = last_gcb;
        return frame;
    }
};

IMPLEMENT_DYNAMIC(CGifView, CStatic)

CGifView::CGifView()
    : mGifFile(nullptr)
    , mOldCanvas(nullptr)
    , mCanvas(nullptr)
    , mMemDcBuffer(nullptr)
    , mShouldStop(true)
    , mBackgroundColor(GetSysColor(COLOR_MENU))
    , mAutoScale(true)
    , mCurrentFrameIdx(0)
    , mCurrentStatus(eStopped)
{
}

CGifView::~CGifView()
{
}


BEGIN_MESSAGE_MAP(CGifView, CStatic)
    ON_WM_DESTROY()
    ON_WM_PAINT()
END_MESSAGE_MAP()
// CGifView 消息处理程序
void CGifView::OnDestroy()
{
    CStatic::OnDestroy();
    //
    Stop();
    //
    ReleaseMemDc();
    //
    if (mGifFile) {
        DGifCloseFile((GifFileType*)mGifFile, nullptr);
    }
}

void CGifView::ReleaseMemDc()
{
    if (mOldCanvas) {
        mMemDc.SelectObject(mOldCanvas);
        mOldCanvas = nullptr;
    }
    mMemDc.DeleteDC();
    if (mCanvas) {
        mCanvas->DeleteObject();
        mCanvas = nullptr;
    }
}

void CGifView::BuildMemDc(int sx, int sy)
{
    // release old one
    ReleaseMemDc();
    //
    CDC *pDC = GetDC();
    mMemDc.CreateCompatibleDC(pDC);
    BITMAPINFO bmi;
    memset(&bmi, 0, sizeof(bmi));
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biSize = sizeof(bmi);
    bmi.bmiHeader.biWidth = sx;
    bmi.bmiHeader.biHeight = -sy; // flip
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biSizeImage = 4 * sx*sy;
    mCanvas = CBitmap::FromHandle(
        CreateDIBSection(mMemDc.GetSafeHdc(), &bmi, DIB_RGB_COLORS, &mMemDcBuffer, 0, 0));
    mOldCanvas = mMemDc.SelectObject(mCanvas);
    ReleaseDC(pDC);

    auto gif = (GifFileType*)mGifFile;
    mCurrentFrameIdx = mCurrentFrameIdx % gif->ImageCount;
    try {
        GraphicsControlBlock gcb;
        if (DGifSavedExtensionToGCB(gif, mCurrentFrameIdx, &gcb) >= 0) {
            GraphicsControlBlock last_gcb = gcb;
            if (mCurrentFrameIdx > 0) {
                DGifSavedExtensionToGCB(gif, mCurrentFrameIdx - 1, &last_gcb);
            }
            auto& frame = Frame::Parse(&gif->SavedImages[mCurrentFrameIdx], gif->SColorMap, gcb, last_gcb);
            // MemDc is shared
            RenderToMemDc(frame);
            mCurrentFrameIdx++;
        }
    }
    catch (const std::exception& e) {

    }
}

bool CGifView::Load(const CString& filename)
{
    // If there's a playing movie, destroy it.
    Stop();
    if (mGifFile) {
        DGifCloseFile((GifFileType*)mGifFile, nullptr);
        mGifFile = nullptr;
    }
    USES_CONVERSION;
    mGifFile = DGifOpenFileName(T2A(filename), nullptr);
    if (mGifFile) {
        GifFileType* gif = (GifFileType*)mGifFile;
        DGifSlurp(gif);
        // Now, build the compatible dc , i.e the virtual canvas.
        BuildMemDc(gif->SWidth, gif->SHeight);
    }
    return mGifFile != nullptr;
}

static void thread_loop(std::function<bool()> func)
{
    while (1) {
        if (!func()) {
            break;
        }
    }
}

void CGifView::Stop()
{
    mShouldStop = true;
    if (mThread.joinable()) {
        mThread.join();
    }
    mCurrentFrameIdx = 0;
    mCurrentStatus = eStopped;
}

void CGifView::Pause()
{
    if (mCurrentStatus == ePlaying) {
        mCurrentStatus = ePaused;
        mShouldStop = true;
        if (mThread.joinable()) {
            mThread.join();
        }
    }
}

void CGifView::Play()
{
    if (mCurrentStatus == ePlaying) return;
    if (mGifFile) {
        mShouldStop = false; // before start the thread, set the flag to false.
        mThread = std::thread(thread_loop, [=]()->bool {
            auto gif = (GifFileType*)mGifFile;
            mCurrentFrameIdx = mCurrentFrameIdx % gif->ImageCount;
            try {
                GraphicsControlBlock gcb;
                if (DGifSavedExtensionToGCB(gif, mCurrentFrameIdx, &gcb) >= 0) {
                    GraphicsControlBlock last_gcb = gcb;
                    if (mCurrentFrameIdx > 0) {
                        DGifSavedExtensionToGCB(gif, mCurrentFrameIdx - 1, &last_gcb);
                    }
                    auto& frame = Frame::Parse(&gif->SavedImages[mCurrentFrameIdx], gif->SColorMap, gcb, last_gcb);
                    // MemDc is shared
                    mMutex.lock();
                    RenderToMemDc(frame);
                    mMutex.unlock();
                    // Refresh
                    Invalidate();
                    //
                    mCurrentFrameIdx++;
                    //
                    for (int k = 0; k <= gcb.DelayTime && !mShouldStop; ++k) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    }
                    //
                    return true && !mShouldStop;
                }
                return false;
            } catch (const std::exception& e) {
                _CRT_UNUSED(e);
                return false;
            }
        });
        mCurrentStatus = ePlaying;
    }
}
#define SET_PIXEL(i,j,c)  do { \
    if (i + y < gif->SHeight && j + x < gif->SWidth) { \
        dc_buffer[bitmap.bmWidthBytes*(i + y) + (j + x)*pb + 0] = c.Blue; \
        dc_buffer[bitmap.bmWidthBytes*(i + y) + (j + x)*pb + 1] = c.Green; \
        dc_buffer[bitmap.bmWidthBytes*(i + y) + (j + x)*pb + 2] = c.Red; \
    } \
} while (0)
void CGifView::RenderToMemDc(const Frame& frame)
{
    auto gif = (GifFileType*)mGifFile;
    // 根据上一个的显示状态来绘制当前帧.
    switch (frame.last_gcb.DisposalMode) {
    case DISPOSE_PREVIOUS:
        break;
    case DISPOSE_BACKGROUND:
        mMemDc.FillSolidRect(0, 0, gif->SWidth, gif->SHeight, mBackgroundColor);
    case DISPOSAL_UNSPECIFIED:
    case DISPOSE_DO_NOT:if (1) {
        BITMAP bitmap;
        mCanvas->GetBitmap(&bitmap);
        int pb = bitmap.bmBitsPixel / 8;
        if (pb >= 3) {
            const auto& color_map = frame.color_map;
            const auto& gcb = frame.gcb;
            const SavedImage* img = frame.img;
            auto& x = img->ImageDesc.Left;
            auto& y = img->ImageDesc.Top;
            auto& w = img->ImageDesc.Width;
            auto& h = img->ImageDesc.Height;
            uint8_t* dc_buffer = (uint8_t*)mMemDcBuffer;
            if (dc_buffer) {
                for (int i = 0; i < h; ++i) {
                    for (int j = 0; j < w; ++j) {
                        int idx = img->RasterBits[w*i + j];
                        if (idx != gcb.TransparentColor) {
                            SET_PIXEL(i, j, color_map->Colors[idx]);
                        }
                    }
                }
            }
        }
    } break;
    }
}

void CGifView::OnPaint()
{
    CPaintDC dc(this);
    // copy to dc.
    CRect rc; GetClientRect(&rc);
    auto gif = (GifFileType*)mGifFile;
    // 当图像过大时, 自动缩放, 保持比例.
    if ((rc.Width() < gif->SWidth || rc.Height() < gif->SHeight) && mAutoScale) {
        double sx = (double)rc.Width() / gif->SWidth;
        double sy = (double)rc.Height() / gif->SHeight;
        double ss = min(sx, sy);
        int w = (int)round(gif->SWidth * ss);
        int h = (int)round(gif->SHeight * ss);
        int x = (rc.Width() - w) / 2, y = (rc.Height() - h) / 2;
        dc.SetStretchBltMode(COLORONCOLOR);
        mMutex.lock();
        dc.StretchBlt(x, y, w, h, &mMemDc, 0, 0, gif->SWidth, gif->SHeight, SRCCOPY);
        mMutex.unlock();
    } else {
        int x = (rc.Width() - gif->SWidth) / 2;
        int y = (rc.Height() - gif->SHeight) / 2;
        mMutex.lock();
        dc.BitBlt(x, y, gif->SWidth, gif->SHeight, &mMemDc, 0, 0, SRCCOPY);
        mMutex.unlock();
    }
}
