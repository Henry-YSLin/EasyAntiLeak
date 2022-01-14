#ifndef EASY_ANTI_LEAK_HPP
#define EASY_ANTI_LEAK_HPP

#include <iostream>
#include <fstream>
#include <string>
#include <cstdlib>
#include <utility>
#include <vector>
#include <queue>
#include <unordered_map>

enum AllocType
{
    AllocType_New,
    AllocType_NewArray,
    AllocType_Delete,
    AllocType_DeleteArray
};

struct AllocEvent
{
    std::string file;
    std::string func;
    int line;
    AllocType type;
    unsigned long long size{};
    void *ptr{};

    AllocEvent(std::string file, std::string func, int line, AllocType type, unsigned long long size, void *ptr)
        : file(std::move(file)), func(std::move(func)), line(line), type(type), size(size), ptr(ptr)
    {
    }

    AllocEvent(std::string file, std::string func, int line, AllocType type)
        : file(std::move(file)), func(std::move(func)), line(line), type(type)
    {
    }
};

std::ostream *allocationLog;
std::vector<AllocEvent> allocationEvents;

struct AllocSizeCompare
{
    bool operator()(const AllocEvent &x, const AllocEvent &y) const
    {
        return x.size > y.size;
    }
};

void setUpTrace(std::ostream *stream)
{
    allocationLog = stream;
    auto func = []()
    {
        *allocationLog << std::endl
                       << "===== Analysis =====" << std::endl;
        unsigned long long totalAlloc = 0;
        unsigned long long totalDealloc = 0;
        std::priority_queue<AllocEvent, std::vector<AllocEvent>, AllocSizeCompare> objectAllocs;
        std::unordered_multimap<void *, AllocEvent> allocated;
        for (auto &event : allocationEvents)
        {
            if (event.type == AllocType_New)
            {
                totalAlloc += event.size;
                objectAllocs.push(event);
                allocated.insert({event.ptr, event});
            }
            else if (event.type == AllocType_NewArray)
            {
                totalAlloc += event.size;
                allocated.insert({event.ptr, event});
            }
            else if (event.type == AllocType_Delete)
            {
                AllocEvent e = objectAllocs.top();
                totalDealloc += e.size;
                allocated.erase(e.ptr);
                objectAllocs.pop();
            }
            else if (event.type == AllocType_DeleteArray)
            {
                AllocEvent e = allocated.find(event.ptr)->second;
                totalDealloc += e.size;
                allocated.erase(e.ptr);
            }
        }
        *allocationLog << "Total allocated size: " << totalAlloc << "B" << std::endl;
        *allocationLog << "Total deallocated size (at least): " << totalDealloc << "B" << std::endl;
        *allocationLog << "Total leaked size (at most): " << totalAlloc - totalDealloc << "B" << std::endl;
        *allocationLog << "Total leaked count: " << allocated.size() << std::endl;
        bool containsObjectLeak = false;
        for (auto &event : allocated)
        {
            if (event.second.type == AllocType_New)
            {
                containsObjectLeak = true;
            }
            else if (event.second.type == AllocType_NewArray)
            {
                *allocationLog << "Confirmed leaked array: " << event.second.size << "B in " << event.second.func
                               << " in " << event.second.file << ":" << event.second.line << std::endl;
            }
        }
        if (containsObjectLeak)
        {
            for (auto &event : allocated)
            {
                if (event.second.type == AllocType_New)
                {
                    *allocationLog << "Possible leaked object: " << event.second.size << "B in " << event.second.func
                                   << " in " << event.second.file << ":" << event.second.line << std::endl;
                }
            }
        }
    };
    std::atexit(func);

    *allocationLog << "===== Runtime trace =====" << std::endl;
}

void setUpTraceFile(const std::string &fileName)
{
    setUpTrace(new std::ofstream(fileName));
}

// class Shim
// {
// private:
//     std::string file;
//     int line;

// public:
//     Shim(std::string file, int line) : file(file), line(line) {}
//     template <typename T>
//     T *operator*(T *p)
//     {
//         std::cout << "new at " << __FILE__ << ":" << __LINE__ << ";p=" << p << std::endl;
//         return p;
//     }
//     template <typename T>
//     void operator/(T *p)
//     {
//         std::cout << "delete at " << __FILE__ << ":" << __LINE__ << ";p=" << p << std::endl;
//         delete p;
//     }
// };

// #define new Shim(__FILE__, __LINE__) * new

void *operator new(size_t size, const char *file, int line, const char *function)
{
    void *ptr = std::malloc(size);
    allocationEvents.emplace_back(file, function, line, AllocType_New, size, ptr);
    *allocationLog << "new size=" << size << "; location=" << function << " in " << file << ":" << line << "; ptr="
                   << ptr << std::endl;
    return ptr;
}

void *operator new[](size_t size, const char *file, int line, const char *function)
{
    void *ptr = std::malloc(size);
    allocationEvents.emplace_back(file, function, line, AllocType_NewArray, size, ptr);
    *allocationLog << "new[] size=" << size << "; location=" << function << " in " << file << ":" << line << "; ptr="
                   << ptr << std::endl;
    return ptr;
}

void operator delete[](void *ptr)
{
    auto lastEvent = std::prev(allocationEvents.end());
    if (lastEvent->type == AllocType_Delete)
    {
        lastEvent->type = AllocType_DeleteArray;
        lastEvent->ptr = ptr;
    }
    *allocationLog << "last delete was delete[] ptr=" << ptr << std::endl;
    std::free(ptr);
}

struct Shim
{
};

void noop()
{
}

int operator+(const Shim &a, const int &b)
{
    return b;
}

#define new +(Shim(), 0) ? nullptr : new (__FILE__, __LINE__, __FUNCTION__)

#define delete +(Shim(), 0) ? noop() : (allocationEvents.push_back(AllocEvent(__FILE__, __FUNCTION__, __LINE__, AllocType_Delete)), *allocationLog << "delete location=" << __FUNCTION__ << " in " << __FILE__ << ":" << __LINE__ << std::endl, 0) ? noop() \
                                                                                                                                                                                                                                                   : delete

#endif