#pragma once

#include <QMetaObject>
#include <QObject>
#include <QPointer>
#include <QThreadPool>
#include <atomic>
#include <functional>
#include <memory>
#include <utility>

// One active query, with independent title and full-text lanes. Never let an
// older completion publish into a new scope or keep pending obsolete work.
template<class Results>
class WizProgressiveSearch final : public QObject {
public:
    using Cancelled = std::function<bool()>;
    using Query = std::function<bool(Results&,const Cancelled&)>;
    using Delivery = std::function<void(bool,Results)>;
    WizProgressiveSearch() {
        local.setMaxThreadCount(1);
        cloud.setMaxThreadCount(1);
    }
    ~WizProgressiveSearch() override { cancelAndWait(); }
    bool active() const { return bool(current); }
    void cancel() {
        if (current) current->canceled=true;
        current.reset();
        local.clear();
        cloud.clear();
    }
    void cancelAndWait() { cancel(); local.waitForDone(); cloud.waitForDone(); }

    void start(Query titles, Query fullText, Delivery titlesReady, Delivery fullTextReady) {
        cancel();
        auto state=std::make_shared<State>();
        state->onTitles=std::move(titlesReady);
        state->onFullText=std::move(fullTextReady);
        current=state;
        submit(local,state,std::move(titles),false);
        if(fullText) submit(cloud,state,std::move(fullText),true);
    }
private:
    struct State {
        std::atomic_bool canceled{false};
        bool titlesDelivered=false, fullTextWaiting=false, fullTextOK=false;
        Results fullText;
        Delivery onTitles,onFullText;
    };
    void submit(QThreadPool& pool,std::shared_ptr<State> s,Query query,bool remote) {
        pool.start([this,s,query=std::move(query),remote] {
            const auto canceled=[s]{return s->canceled.load();};
            if(canceled()) return;
            Results result;
            const bool ok=query(result,canceled);
            if(!canceled()) QMetaObject::invokeMethod(this,
                [this,s,ok,remote,result=std::move(result)]() mutable {
                    if(current!=s || s->canceled) return;
                    if(remote) {
                        if(s->titlesDelivered) s->onFullText(ok,std::move(result));
                        else {
                            s->fullTextWaiting=true;
                            s->fullTextOK=ok;
                            s->fullText=std::move(result);
                        }
                    } else {
                        const QPointer<WizProgressiveSearch> alive(this);
                        s->onTitles(ok,std::move(result));
                        if(!alive || current!=s || s->canceled) return;
                        s->titlesDelivered=true;
                        if(s->fullTextWaiting) s->onFullText(s->fullTextOK,std::move(s->fullText));
                    }
                }, Qt::QueuedConnection);
        });
    }
    QThreadPool local,cloud;
    std::shared_ptr<State> current;
};
