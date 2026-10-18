#include "WizThreads.h"
#include "WizThreads_p.h"
#include "WizQtCompat.h"

#include <QThread>
#include <QMutex>
#include <QWaitCondition>
#include <QDateTime>
#include <QTimer>
#include <QCoreApplication>

#include <deque>
#include <vector>
#include <map>
#include <atomic>

#ifdef WIZNOTE_THREAD_POOL_TEST_HOOKS
// Test-only scheduling barrier, absent from the production executable.
extern void wizThreadPoolBeforeWaitForTest();
#endif

class CWizTaskWorkThread;
class CWizThreadPool;

typedef CWizTaskWorkThread* WizCreateThreadFunction(CWizThreadPool* pool, int threadIndex, QThread::Priority priority);


class CWizThreadPool : public IWizDelayedThreadPool
{
public:
    CWizThreadPool(int poolCount, WizCreateThreadFunction* createThreadFun, QThread::Priority priority);
    ~CWizThreadPool();
protected:
    std::deque<CWizTaskWorkThread*> m_threads;
    std::deque<IWizRunable*> m_tasks;

    WizRecursiveMutex m_cs;
    QMutex m_csEvent;
    QWaitCondition m_event;
    std::atomic_bool m_bShuttingDown;
    std::atomic<IWizThreadPoolEvents*> m_pEvents;
public:
    virtual void addTask(IWizRunable* task);
    virtual void clearTasks();
    virtual IWizRunable* peekOne();    //execute on worker
    //
    virtual bool isShuttingDown();
    virtual void shutdown(int timeout);
    virtual bool isIdle();
    virtual void setEventsListener(IWizThreadPoolEvents* pEvents);
    virtual void getTaskCount(int* pnWorking, int* pnWaiting);
    //
    virtual void addDelayedTask(IWizRunable* task, int delayedSeconds) { Q_UNUSED(task); Q_UNUSED(delayedSeconds); }
    virtual void executeAllNow() {}
public:
    void beforeTask(IWizRunable* task);
    void afterTask(IWizRunable* task);
protected:
    int GetWorkingTaskCount();
    int GetWaitingTaskCount();
};


class CWizTaskWorkThread : public QThread
{
public:
    CWizTaskWorkThread(CWizThreadPool* pool, int threadIndex)
        : m_pool(pool)
        , m_nThreadIndex(threadIndex)
        , n_bWorking(false)
    {
    }
    virtual ~CWizTaskWorkThread()
    {
//        qDebug() << "CWizTaskWorkThread destruct";
    }
protected:
    CWizThreadPool* m_pool;
    int m_nThreadIndex;
    std::atomic_bool n_bWorking;
protected:
    virtual void run()
    {
        while (!m_pool->isShuttingDown())
        {            
            //
            IWizRunable* task = m_pool->peekOne();
            if (task)
            {
                m_pool->beforeTask(task);
                n_bWorking = true;
                task->run(m_nThreadIndex, m_pool, NULL);
                n_bWorking = false;
                m_pool->afterTask(task);
                task->destroy();
            }
        }
    }
    //
public:
    virtual IWizThreadPool* getThreadPool()
    {
        return m_pool;
    }
    //
    virtual bool IsAlive()
    {
        return isRunning();
    }
    //
    virtual bool IsWorking()
    {
        return n_bWorking;
    }
public:
    static CWizTaskWorkThread* Create(CWizThreadPool* pool, int threadIndex, QThread::Priority priority)
    {
        CWizTaskWorkThread* thread = new CWizTaskWorkThread(pool, threadIndex);
        thread->start(priority);
        return thread;
    }
};

CWizThreadPool::CWizThreadPool(int poolCount, WizCreateThreadFunction* createThreadFun, QThread::Priority priority)
    : m_cs()
    , m_csEvent()
    , m_bShuttingDown(false)
    , m_pEvents(NULL)
{
    //
    for (int i = 0; i < poolCount; i++)
    {
        CWizTaskWorkThread* thread = createThreadFun(this, i, priority);
        m_threads.push_back(thread);
    }
}

CWizThreadPool::~CWizThreadPool()
{
    //qDebug() << "CWizThreadPool destruct , threads : " << m_threads.size();
    for (CWizTaskWorkThread* thread : m_threads)
    {
        QObject::connect(thread, SIGNAL(finished()), thread, SLOT(deleteLater()));
        thread->quit();
    }
    m_threads.clear();
    clearTasks();
}
//
void CWizThreadPool::addTask(IWizRunable* task)
{
    QString strTaskId = task->getTaskID();
    std::deque<IWizRunable*> discarded;
    bool queued = false;
    {
        QMutexLocker lock(&m_cs);
        if (m_bShuttingDown) discarded.push_back(task);
        else
        {
            if (std::find(m_tasks.begin(), m_tasks.end(), task) != m_tasks.end()) return;
            if (!strTaskId.isEmpty())
            {
                for (auto it = m_tasks.begin(); it != m_tasks.end();)
                {
                    if ((*it)->getTaskID() == strTaskId) {
                        discarded.push_back(*it);
                        it = m_tasks.erase(it);
                    } else ++it;
                }
            }
            m_tasks.push_back(task);
            queued = true;
        }
    }
    if (queued) {
        QMutexLocker lockEvent(&m_csEvent);
        m_event.wakeAll();
    }
    // Canceled work must release captured resources, but never execute it.
    // Destructors may reenter the pool; do not call them under either mutex.
    for (auto* old : discarded) old->destroy();
}

void CWizThreadPool::clearTasks()
{
    std::deque<IWizRunable*> discarded;
    {
        QMutexLocker lock(&m_cs);
        discarded.swap(m_tasks);
    }
    for (auto* task : discarded) task->destroy();
}

IWizRunable* CWizThreadPool::peekOne()    //execute on worker
{
    if (m_bShuttingDown)
        return nullptr;
    //
    IWizRunable* task = nullptr;
    //
    //
    //
    {
        QMutexLocker lock(&m_cs);
        if (m_bShuttingDown)
            return nullptr;
        //
        if (!m_tasks.empty())
        {
            task = *m_tasks.begin();
            m_tasks.pop_front();
        }
    }

    //
    if (task)
        return task;
    //
    {
#ifdef WIZNOTE_THREAD_POOL_TEST_HOOKS
        wizThreadPoolBeforeWaitForTest();
#endif
        QMutexLocker locker(&m_csEvent);
        while (true)
        {
            // Recheck the queue while holding the notification mutex. A
            // producer cannot notify between this predicate and wait's atomic
            // unlock, even if its first notification arrived before this lock.
            {
                QMutexLocker lock(&m_cs);
                if (m_bShuttingDown) return nullptr;
                if (!m_tasks.empty()) {
                    task = m_tasks.front();
                    m_tasks.pop_front();
                    return task;
                }
            }
            m_event.wait(&m_csEvent);
        }
    }
    //
    return task;
}
//
bool CWizThreadPool::isShuttingDown()
{
    return m_bShuttingDown;
}
//
void CWizThreadPool::shutdown(int timeout)
{
    m_bShuttingDown = true;
    //
    //
    {
        QMutexLocker lock(&m_csEvent);
        m_event.wakeAll();
    }
    //
    //
    Q_UNUSED(timeout);
    //while ((int)WizGetTickCount() - start >= timeout)
    while (true)
    {
        QMutexLocker lock(&m_cs);
        int count = (int)m_threads.size();
        for (int i = count - 1; i >= 0; i--)
        {
            CWizTaskWorkThread* thread = m_threads[i];
            //
            if (!thread->IsAlive())
            {
                m_threads.erase(m_threads.begin() + i);
                thread->deleteLater();
            }
            else
            {
                thread->quit();
            }
        }
        //

        if (m_threads.empty())
        {
            lock.unlock();
            delete this;
            return;
        }
        lock.unlock();
        //
        //WizProcessMessages();
#ifdef Q_OS_LINUX
        class WaitThread : public QThread
        {
         public :
             static void sleep(long iSleepTime)
             {
                  QThread::sleep(iSleepTime);
             }
             static void msleep(long iSleepTime)
             {
                  QThread::msleep(iSleepTime);
             }
        };
        WaitThread::msleep(100);
#else
        QThread::msleep(100);
#endif
    }
}
//
void CWizThreadPool::getTaskCount(int* pnWorking, int* pnWaiting)
{
    if (pnWorking)
    {
        *pnWorking = GetWorkingTaskCount();
    }
    if (pnWaiting)
    {
        *pnWaiting = GetWaitingTaskCount();
    }
}
//
int CWizThreadPool::GetWorkingTaskCount()
{
    QMutexLocker lock(&m_cs);
    int count = 0;
    //
    for (auto it = m_threads.begin(); it != m_threads.end(); it++)
    {
        CWizTaskWorkThread* thread = *it;
        if (thread->IsWorking())
        {
            count++;
        }
    }
    //
    return count;
}
//
bool CWizThreadPool::isIdle()
{
    QMutexLocker lock(&m_cs);
    if (GetWaitingTaskCount() > 0)
        return false;
    //
    for (auto it = m_threads.begin(); it != m_threads.end(); it++)
    {
        CWizTaskWorkThread* thread = *it;
        if (thread->IsWorking())
        {
            return false;
        }
    }
    return true;
}
void CWizThreadPool::setEventsListener(IWizThreadPoolEvents* pEvents)
{
    m_pEvents = pEvents;
}


//
int CWizThreadPool::GetWaitingTaskCount()
{
    QMutexLocker locker(&m_cs);
    int count = (int)m_tasks.size();
    //
    return count;
}

void CWizThreadPool::beforeTask(IWizRunable* task)
{
    auto* events = m_pEvents.load();
    if (!events)
        return;
    events->beforeTask(task);
}
void CWizThreadPool::afterTask(IWizRunable* task)
{
    auto* events = m_pEvents.load();
    if (!events)
        return;
    events->afterTask(task);
}



IWizThreadPool* WizCreateThreadPool(int threadCount, QThread::Priority priority)
{
    CWizThreadPool* pool = new CWizThreadPool(threadCount, CWizTaskWorkThread::Create, priority);
    return pool;
}

///////////////////////////////////////////////////////////////////////////////////

class CWizDelayedWorkThread : public CWizTaskWorkThread
{
public:
    CWizDelayedWorkThread(CWizThreadPool* pool, int threadIndex)
        : CWizTaskWorkThread(pool, threadIndex)
    {
    }
protected:
    virtual void run()
    {
        while (1)
        {
            if (m_pool->isShuttingDown())
                return;
            //
            IWizRunable* task = m_pool->peekOne();
            if (task)
            {
                m_pool->beforeTask(task);
                n_bWorking = true;
                task->run(m_nThreadIndex, m_pool, NULL);
                n_bWorking = false;
                m_pool->afterTask(task);
                task->destroy();
            }
            else
            {
                QThread::sleep(1);
            }
        }
    }
public:
    static CWizTaskWorkThread* Create(CWizThreadPool* pool, int threadIndex, QThread::Priority priority)
    {
        CWizDelayedWorkThread* thread = new CWizDelayedWorkThread(pool, threadIndex);
        thread->start(priority);
        return thread;
    }
};


class CWizDelayedThreadPool : public CWizThreadPool
{
public:
    CWizDelayedThreadPool(int poolCount, WizCreateThreadFunction* createThreadFunction, QThread::Priority priority)
        : CWizThreadPool(poolCount, createThreadFunction, priority)
    {
    }
protected:
    std::map<IWizRunable*, QDateTime> m_mapTasksTime;
public:
    //
    virtual void addTask(IWizRunable* task)
    {
        addDelayedTask(task, 0);
    }
    //
    virtual void addDelayedTask(IWizRunable* task, int delayedSeconds)
    {
        CWizThreadPool::addTask(task);
        //
        QMutexLocker lock(&m_cs);
        //
        QDateTime now = QDateTime::currentDateTime();
        QDateTime work = now.addSecs(delayedSeconds);
        //
        m_mapTasksTime[task] = work;
    }
    virtual void executeAllNow()
    {
        QMutexLocker lock(&m_cs);
        //
        m_mapTasksTime.clear();
        QDateTime now = QDateTime::currentDateTime();
        //
        for (auto it = m_tasks.begin();
            it != m_tasks.end();
            it++)
        {
            m_mapTasksTime[*it] = now;
        }
    }
    //
    virtual IWizRunable* peekOne()    //execute on worker
    {
        if (m_bShuttingDown)
            return nullptr;
        //
        QDateTime now = QDateTime::currentDateTime();
        //
        QMutexLocker lock(&m_cs);

        if (m_bShuttingDown)
            return nullptr;

        //
        if (m_tasks.empty())
            return nullptr;
        //
        size_t count = m_tasks.size();
        for (size_t i = 0; i < count; i++)
        {
            IWizRunable* task = m_tasks[i];
            //
            QDateTime t = m_mapTasksTime[task];
            //
            if (now >= t)
            {
                m_tasks.erase(m_tasks.begin() + i);
                return task;
            }
        }
        //
        return nullptr;
    }
};

IWizDelayedThreadPool* WizCreateDelayedThreadPool(int threadCount, QThread::Priority priority)
{
    CWizDelayedThreadPool* pool = new CWizDelayedThreadPool(threadCount, CWizDelayedWorkThread::Create, priority);
    return pool;
}
///////////////////////////////////////////////////////////////////////////////////

WizMainQueuedThread::WizMainQueuedThread()
    : QObject(QCoreApplication::instance() && QCoreApplication::instance()->thread() == QThread::currentThread()
                  ? QCoreApplication::instance() : nullptr)
    , m_bShutingDown(false)
{
    connect(this, SIGNAL(taskAdded()), this, SLOT(executeAllActions()), Qt::QueuedConnection);
}
WizMainQueuedThread::~WizMainQueuedThread()
{
    m_bShutingDown = true;
    clearTasks();
}
void WizMainQueuedThread::shutdown(int timeout)
{
    Q_UNUSED(timeout);
    m_bShutingDown = true;
    clearTasks();
}
void WizMainQueuedThread::addTask(IWizRunable* task)
{
    bool accepted = false;
    {
        QMutexLocker lock(&m_cs);
        if (!m_bShutingDown) {
            m_tasks.push_back(task);
            accepted = true;
        }
    }
    if (accepted) emit taskAdded();
    else task->destroy();
}
//
void WizMainQueuedThread::clearTasks()
{
    std::deque<IWizRunable*> discarded;
    {
        QMutexLocker lock(&m_cs);
        discarded.swap(m_tasks);
    }
    for (auto* task : discarded) task->destroy();
}

//
void WizMainQueuedThread::getTaskCount(int* pnWorking, int* pnWaiting)
{
    QMutexLocker lock(&m_cs);
    //
    if (pnWaiting)
    {
        *pnWaiting = (int)m_tasks.size();
    }
    //
    if (pnWorking)
    {
        *pnWorking = 1;
    }
}
//
void WizMainQueuedThread::getAllTasks(std::deque<IWizRunable*>& tasks)
{
    QMutexLocker lock(&m_cs);
    tasks = m_tasks;
    m_tasks.clear();
}
void WizMainQueuedThread::executeAllActions()
{
    std::deque<IWizRunable*> tasks;
    getAllTasks(tasks);
    //
    while (!tasks.empty())
    {
        IWizRunable* pRunable = tasks.front();
        tasks.pop_front();
        if (!isShuttingDown()) pRunable->run(WIZ_THREAD_MAIN, this, NULL);
        pRunable->destroy();

    }
}


WizTimeoutRunable::WizTimeoutRunable(IWizRunable* pAction, int milliseconds, int nTimeoutThreadId, IWizRunable* pTimeoutAction)
    : m_pRunable(pAction)
    , m_nTimeoutThreadId(nTimeoutThreadId)
    , m_pTimeoutRunable(pTimeoutAction)
{
    m_timer = new QTimer(NULL);
    m_timer->setSingleShot(true);
    m_timer->setInterval(milliseconds);
    connect(m_timer, SIGNAL(timeout()), this, SLOT(timeout()));
}
void WizTimeoutRunable::destroy()
{
    m_pRunable->destroy();
    delete this;
}
void WizTimeoutRunable::run(int threadIndex, IWizThreadPool* pThreadPool, IWizRunableEvents* pEvents)
{
    m_pRunable->run(threadIndex, pThreadPool, pEvents);
}
QString WizTimeoutRunable::getTaskID()
{
    return m_pRunable->getTaskID();
}


IWizRunable* WizTimeoutRunable::beforeTimeout()
{
    QMutexLocker lock(&m_cs);
    //
    IWizRunable* runable = m_pTimeoutRunable;
    m_pTimeoutRunable = NULL;
    //
    return runable;
}


void WizTimeoutRunable::timeout()
{
    if (IWizRunable* pRunable = beforeTimeout())
    {
        WizExecuteOnThread<IWizRunable*>(m_nTimeoutThreadId, pRunable);
    }
}


class CWizQueuedThreads
{
private:
    static QMutex& GetCriticalSection()
    {
        static QMutex cs;
        return cs;
    }
    static std::map<int, IWizThreadPool*>& GetThreads()
    {
        static std::map<int, IWizThreadPool*> threads;
        return threads;
    }
    //
    static IWizThreadPool* GetMainThreadPool()
    {
        // Retired queues remain application-owned while an executing slot
        // unwinds; explicit reinitialization receives a fresh main queue.
        return new WizMainQueuedThread();
    }
    static bool& Closing() { static bool value = false; return value; }
    static bool& Clearing() { static bool value = false; return value; }
    static int& Submissions() { static int value = 0; return value; }
    static QWaitCondition& SubmissionFinished() { static QWaitCondition value; return value; }
    struct SubmissionLease {
        ~SubmissionLease() {
            QMutexLocker lock(&GetCriticalSection());
            --Submissions();
            SubmissionFinished().wakeAll();
        }
    };
    // Caller holds the registry lock.
    static IWizThreadPool* getThreadPoolLocked(int threadID, int threadCount = 1)
    {
        //
        std::map<int, IWizThreadPool*>& threads = GetThreads();
        //
        std::map<int, IWizThreadPool*>::const_iterator it = threads.find(threadID);
        if (it != threads.end())
            return it->second;
        //
        if (threadID == WIZ_THREAD_MAIN)
        {
            IWizThreadPool* pool = GetMainThreadPool();
            threads[threadID] = pool;
            return pool;
        }
        //
        IWizThreadPool* pool = ::WizCreateThreadPool(threadCount);
        threads[threadID] = pool;
        return pool;
    }
    //

public:
    static void initialize()
    {
        QMutexLocker lock(&GetCriticalSection());
        if (Clearing()) return;
        Closing() = false;
        getThreadPoolLocked(WIZ_THREAD_MAIN);
        getThreadPoolLocked(WIZ_THREAD_DOWNLOAD_RESOURCES, 5);
        getThreadPoolLocked(WIZ_THREAD_NETWORK, 2);
    }
    static void addAction(int threadID, IWizRunable* action)
    {
        IWizThreadPool* pool = nullptr;
        {
            QMutexLocker lock(&GetCriticalSection());
            if (!Closing()) {
                pool = getThreadPoolLocked(threadID);
                ++Submissions();
            }
        }
        if (!pool) { action->destroy(); return; }
        // Keep the pool alive through enqueue/discard callbacks, without
        // holding the registry mutex across those reentrant callbacks.
        SubmissionLease lease;
        pool->addTask(action);
    }
    static void ClearThreadPool()
    {
        std::map<int, IWizThreadPool*> threads;
        {
            QMutexLocker lock(&GetCriticalSection());
            if (Clearing()) return;
            Clearing() = true;
            Closing() = true;
            while (Submissions()) SubmissionFinished().wait(&GetCriticalSection());
            threads.swap(GetThreads());
        }
        qDebug() << "clear thread pool, threads count : " << threads.size();
        std::map<int, IWizThreadPool*>::const_iterator it;
        for (it = threads.begin(); it != threads.end(); it++)
        {
            IWizThreadPool* threadPool = it->second;
            threadPool->shutdown(5);
        }
        // A finishing worker may submit a completion: do not hold the registry
        // mutex while waiting for it. Closing submissions release their work.
        QMutexLocker lock(&GetCriticalSection());
        Clearing() = false;
    }
};

void WizQueuedThreadsInit()
{
    CWizQueuedThreads::initialize();
}

void WizQueuedThreadAddAction(int threadID, IWizRunable* action)
{
    CWizQueuedThreads::addAction(threadID, action);
}

void WizQueuedThreadAddAction(int threadID, IWizRunable* action, int milliseconds, int nTimeoutThreadId, IWizRunable* timeoutAction)
{
    CWizQueuedThreads::addAction(threadID, new WizTimeoutRunable(action, milliseconds, nTimeoutThreadId, timeoutAction));
}


void WizQueuedThreadsShutdown()
{
//    CWizQueuedThreads::ShutdownAll();

    CWizQueuedThreads::ClearThreadPool();
}
