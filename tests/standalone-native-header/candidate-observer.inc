// Unaccepted experiment: native sharing overlays require separate acceptance.
// Never included by the main client; retained only for isolated comparison.
namespace {
void applyUnifiedHeaderGeometry(QWidget* window, int height)
{
    // Preserve Qt's delegate and event dispatch. Only lay out the existing
    // AppKit controls; never install the legacy proxy or swizzle native methods.
    NSView* view = reinterpret_cast<NSView*>(window->winId());
    if (view.window.titleVisibility!=NSWindowTitleHidden) view.window.titleVisibility=NSWindowTitleHidden;
    if (window->isFullScreen() || (view.window.styleMask & NSWindowStyleMaskFullScreen)) return;
    NSButton* close = [view.window standardWindowButton:NSWindowCloseButton];
    NSView* container = close.superview;
    if (!container || !container.superview || height <= 0) return;
    NSRect frame = container.frame;
    frame.size.height = height;
    frame.origin.y = container.superview.flipped ? 0 : NSHeight(container.superview.bounds) - height;
    if (!NSEqualRects(container.frame,frame)) container.frame = frame;
    for (NSWindowButton type : {NSWindowCloseButton, NSWindowMiniaturizeButton, NSWindowZoomButton}) {
        NSButton* control = [view.window standardWindowButton:type];
        NSPoint origin = control.frame.origin;
        origin.y = (height - NSHeight(control.frame)) / 2;
        if (!NSEqualPoints(control.frame.origin,origin)) [control setFrameOrigin:origin];
    }
}

// AppKit can retile its own buttons without a Qt resize/state notification
// (for example when the window's selective-sharing state changes). Observe
// only this window's native geometry and repair after that native layout has
// returned. Keep Qt's delegate, button targets/actions and fullscreen behavior.
class UnifiedHeaderObserver final : public QObject
{
public:
    explicit UnifiedHeaderObserver(QWidget* window) : QObject(window),m_window(window)
    {
        setObjectName(QStringLiteral("wiz-native-unified-header-observer"));
        window->installEventFilter(this);
    }
    ~UnifiedHeaderObserver() override { detach(); }

    void update(int height)
    {
        m_height=height;
        if (!m_window||!m_window->windowHandle()) return;
        NSView* view=reinterpret_cast<NSView*>(m_window->winId());
        NSButton* close=[view.window standardWindowButton:NSWindowCloseButton];
        NSView* container=close.superview;
        if (!close||!container) return;
        if (m_views&&[m_views objectAtIndex:0]==close&&[m_views objectAtIndex:1]==container) return;
        detach();
        m_views=[[NSArray alloc] initWithObjects:close,container,nil];
        m_tokens=[[NSMutableArray alloc] init];
        const QPointer<UnifiedHeaderObserver> self(this);
        for (NSView* nativeView in m_views) {
            m_oldNotifications.push_back(nativeView.postsFrameChangedNotifications);
            nativeView.postsFrameChangedNotifications=YES;
            id token=[[NSNotificationCenter defaultCenter] addObserverForName:NSViewFrameDidChangeNotification
                object:nativeView queue:nil usingBlock:^(NSNotification*) { if (self) self->schedule(); }];
            [m_tokens addObject:token];
        }
    }

protected:
    bool eventFilter(QObject* watched,QEvent* event) override
    {
        if (watched==m_window&&(event->type()==QEvent::Show||event->type()==QEvent::WindowStateChange
            ||event->type()==QEvent::WindowActivate||event->type()==QEvent::WindowUnblocked
            ||event->type()==QEvent::WinIdChange)) schedule();
        return false;
    }

private:
    void detach()
    {
        for (id token in m_tokens) [[NSNotificationCenter defaultCenter] removeObserver:token];
        [m_tokens release];m_tokens=nil;
        for (NSUInteger index=0;index<m_views.count;++index)
            [(NSView*)[m_views objectAtIndex:index] setPostsFrameChangedNotifications:m_oldNotifications[index]];
        [m_views release];m_views=nil;
        m_oldNotifications.clear();
    }
    void schedule()
    {
        if (m_scheduled) return;
        m_scheduled=true;
        QTimer::singleShot(0,this,[this] {
            m_scheduled=false;
            if (!m_window||!m_window->isVisible()||!m_window->windowHandle()) return;
            update(m_height);
            applyUnifiedHeaderGeometry(m_window,m_height);
        });
    }
    QPointer<QWidget> m_window;
    int m_height=0;
    bool m_scheduled=false;
    NSArray* m_views=nil;
    NSMutableArray* m_tokens=nil;
    std::vector<bool> m_oldNotifications;
};
}

void wizMacConfigureUnifiedHeader(QWidget* window, int height)
{
    if (!window||height<=0) return;
    applyUnifiedHeaderGeometry(window,height);
    auto* object=window->findChild<QObject*>(QStringLiteral("wiz-native-unified-header-observer"),Qt::FindDirectChildrenOnly);
    auto* observer=dynamic_cast<UnifiedHeaderObserver*>(object);
    if (!observer) observer=new UnifiedHeaderObserver(window);
    observer->update(height);
}
