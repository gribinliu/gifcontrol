///
/// (C) 2019 CHIV
///
///
#pragma once
// CGifView
#include <mutex>
#include <thread>
class CGifView : public CStatic
{
	DECLARE_DYNAMIC(CGifView)

public:
	CGifView();
	virtual ~CGifView();
    //
    enum PlayerStatus {
        ePlaying,
        ePaused,
        eStopped
    };
    PlayerStatus GetPlayerStatus() const { return mCurrentStatus; }
    //
    bool Load(const CString& filename);
    void Play();
    void Stop();
    void Pause();
    void SetAutoScale(bool auto_scale)
    {
        mAutoScale = auto_scale;
    }
    void SetBackgroundColor(const COLORREF& background_color)
    {
        mBackgroundColor = background_color;
    }
    //
private:
    PlayerStatus mCurrentStatus;
    int mCurrentFrameIdx;
    bool mAutoScale;
    COLORREF mBackgroundColor;
    struct Frame;
    std::mutex mMutex;          // protect current frame, cache, .etc
    std::thread mThread;        // decode thread.
    volatile bool mShouldStop;  // flag to exit the thread.
    void* mGifFile;             // GIF FILE HANDLE.
    CDC mMemDc;
    void* mMemDcBuffer;
    //
    CBitmap* mCanvas;
    CBitmap* mOldCanvas;
    void BuildMemDc(int sx, int sy);
    void ReleaseMemDc();
    //
    void RenderToMemDc(const Frame& frame);
    //
protected:
	DECLARE_MESSAGE_MAP()
    afx_msg void OnDestroy();
    afx_msg void OnPaint();
};


