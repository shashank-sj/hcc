#ifndef __CLAMP_SERIALIZE
#define __CLAMP_SERIALIZE

#include <amp_runtime.h>
#include <stdio.h>

namespace Concurrency
{

/// traverse all the buffers that are going to be used in kernel
class FunctorBufferWalker {
public:
    virtual void Append(size_t sz, const void* s) {}
    virtual void AppendPtr(size_t sz, const void* s) {}
    virtual void Push(struct rw_info* rw, bool modify, bool isArray) = 0;
};

/// This is used to avoid incorrect compiler error
class Serialize {
    FunctorBufferWalker* vis;
public:
    Serialize(FunctorBufferWalker* vis) : vis(vis) {}
    void Append(size_t sz, const void* s) { vis->Append(sz, s); }
    void AppendPtr(size_t sz, const void* s) { vis->AppendPtr(sz, s); }
    void Push(struct rw_info* rw, bool modify, bool isArray) {
        vis->Push(rw, modify, isArray);
    }
};

/// Change the data pointer with device pointer
/// before/after kernel launches in cpu path
class CPUVisitor : public FunctorBufferWalker
{
    std::shared_ptr<KalmarQueue> pQueue;
    std::set<struct rw_info*> bufs;
public:
    CPUVisitor(std::shared_ptr<KalmarQueue> pQueue) : pQueue(pQueue) {}
    void Push(struct rw_info* rw, bool modify, bool isArray) override {
        if (isArray) {
            auto curr = pQueue->getDev()->get_path();
            auto path = rw->master->getDev()->get_path();
            if (path == L"cpu") {
                auto asoc = rw->stage->getDev()->get_path();
                if (asoc == L"cpu" || path != curr)
                    throw runtime_exception(__errorMsg_UnsupportedAccelerator, E_FAIL);
            }
        }
        rw->sync(pQueue, modify, false);
        if (bufs.find(rw) == std::end(bufs)) {
            void*& device = rw->devs[pQueue->getDev()].data;
            void*& data = rw->data;
            bufs.insert(rw);
            std::swap(device, data);
        }
    }
};

/// Append kernel argument to kernel
class BufferArgumentsAppender : public FunctorBufferWalker
{
    std::shared_ptr<KalmarQueue> pQueue;
    void* k_;
    int current_idx_;
public:
    BufferArgumentsAppender(std::shared_ptr<KalmarQueue> pQueue, void* k)
        : pQueue(pQueue), k_(k), current_idx_(0) {}
    void Append(size_t sz, const void *s) override {
        CLAMP::PushArg(k_, current_idx_++, sz, s);
    }
    void AppendPtr(size_t sz, const void *s) override {
        CLAMP::PushArgPtr(k_, current_idx_++, sz, s);
    }
    void Push(struct rw_info* rw, bool modify, bool isArray) override {
        if (isArray) {
            auto curr = pQueue->getDev()->get_path();
            auto path = rw->master->getDev()->get_path();
            if (path == L"cpu") {
                auto asoc = rw->stage->getDev()->get_path();
                if (asoc == L"cpu" || path != curr)
                    throw runtime_exception(__errorMsg_UnsupportedAccelerator, E_FAIL);
            }
        }
        rw->sync(pQueue, modify, false);
        pQueue->Push(k_, current_idx_++, rw->devs[pQueue->getDev()].data, modify);
    }
};

class QueueSelector : public FunctorBufferWalker
{
public:
    QueueSelector() : collector() {}
    /// select the queue that most of input containers reside
    std::shared_ptr<KalmarQueue> best() {
        std::sort(std::begin(collector), std::end(collector));
        std::vector<std::shared_ptr<KalmarQueue>> candidate;
        int max = 0;
        for (int i = 0; i < collector.size(); ++i) {
            auto head = collector[i];
            int count = 1;
            while (head == collector[++i])
                ++count;
            if (count > max) {
                max = count;
                candidate.clear();
                candidate.push_back(head);
            }
        }
        if (candidate.size())
            return candidate[0];
        else
            return nullptr;
    }
    void Push(struct rw_info* rw, bool modify, bool isArray) {
        if (isArray && rw->stage->getDev()->get_path() != L"cpu")
            collector.push_back(rw->stage);
    }
private:
    std::vector<std::shared_ptr<KalmarQueue>> collector;
};

} // namespace Concurrency

#endif // __CLAMP_SERIALIZE
