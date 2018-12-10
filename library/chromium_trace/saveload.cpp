#include "saveload.h"

#include <util/stream/input.h>
#include <util/stream/output.h>
#include <util/system/yassert.h>

#include <type_traits>

using namespace NChromiumTrace;

namespace {
    static void SaveStr(IOutputStream* out, TStringBuf str) {
        ::SaveSize(out, str.size());
        ::SavePodArray(out, str.data(), str.size());
    }

    static void LoadStr(IInputStream* in, TStringBuf& str, TMemoryPool& pool) {
        size_t size = ::LoadSize(in);
        char* data = ::AllocateFromPool(pool, size);
        ::LoadPodArray(in, data, size);

        str = TStringBuf(data, size);
    }

    using TConstAnyEventPtr = TVariant<
        const TDurationBeginEvent*,
        const TDurationEndEvent*,
        const TDurationCompleteEvent*,
        const TInstantEvent*,
        const TAsyncEvent*,
        const TCounterEvent*,
        const TMetadataEvent*>;

    struct TGetTagVisitor {
        template <typename T>
        constexpr i8 operator()(const T*) const {
            return TAnyEvent::TagOf<T>();
        }
    };

    struct TSavePtrVisitor {
        IOutputStream* Out;

        template <typename T>
        void operator()(const T* event) const {
            Y_ASSERT(event);
            ::Save(Out, *event);
        }
    };

    struct TSaveVisitor {
        IOutputStream* Out;

        template <typename T>
        void operator()(T value) const {
            ::Save(Out, value);
        }
    };

    template <>
    inline void TSaveVisitor::operator()(TStringBuf value) const {
        ::SaveStr(Out, value);
    }

    struct TEventWithArgsPtr {
        TConstAnyEventPtr Event;
        const TEventArgs* Args;

        void Save(IOutputStream* out) const {
            static const TEventArgs emptyArgs;

            i8 tag = Visit(TGetTagVisitor(), Event);
            ::Save(out, tag);
            Visit(TSavePtrVisitor{out}, Event);
            if (Args) {
                ::Save(out, *Args);
            } else {
                ::Save(out, emptyArgs);
            }
        }
    };

    struct TSaveAnyEventVisitor {
        IOutputStream* Out;
        const TEventArgs* Args;

        template <typename T>
        void operator()(const T& value) const {
            ::Save(Out, TEventWithArgsPtr{&value, Args});
        }
    };
}

#define CHECK_EVENT_TAG_I8(type) static_assert( \
    TAnyEvent::TagOf<type>() <= 127 && TAnyEvent::TagOf<type>() != TVARIANT_NPOS && \
    TConstAnyEventPtr::TagOf<const type*>() <= 127 && \
    TConstAnyEventPtr::TagOf<const type*>() != TVARIANT_NPOS, \
    "tag of " #type " is too big")

#define CHECK_ARG_TAG_I8(type) static_assert( \
    TEventArgs::TArg::TValue::TagOf<type>() <= 127 && \
    TEventArgs::TArg::TValue::TagOf<type>() != TVARIANT_NPOS, \
    "tag of " #type " is too big")

CHECK_EVENT_TAG_I8(TDurationBeginEvent);
CHECK_EVENT_TAG_I8(TDurationEndEvent);
CHECK_EVENT_TAG_I8(TDurationCompleteEvent);
CHECK_EVENT_TAG_I8(TInstantEvent);
CHECK_EVENT_TAG_I8(TAsyncEvent);
CHECK_EVENT_TAG_I8(TCounterEvent);
CHECK_EVENT_TAG_I8(TMetadataEvent);

CHECK_ARG_TAG_I8(i64);
CHECK_ARG_TAG_I8(double);
CHECK_ARG_TAG_I8(TStringBuf);

TSaveLoadTraceConsumer::TSaveLoadTraceConsumer(IOutputStream* stream)
    : Stream(stream)
{
}

void TSaveLoadTraceConsumer::AddEvent(const TDurationBeginEvent& event, const TEventArgs* args) {
    ::Save(Stream, TEventWithArgsPtr{&event, args});
}

void TSaveLoadTraceConsumer::AddEvent(const TDurationEndEvent& event, const TEventArgs* args) {
    ::Save(Stream, TEventWithArgsPtr{&event, args});
}

void TSaveLoadTraceConsumer::AddEvent(const TDurationCompleteEvent& event, const TEventArgs* args) {
    ::Save(Stream, TEventWithArgsPtr{&event, args});
}

void TSaveLoadTraceConsumer::AddEvent(const TCounterEvent& event, const TEventArgs* args) {
    ::Save(Stream, TEventWithArgsPtr{&event, args});
}

void TSaveLoadTraceConsumer::AddEvent(const TMetadataEvent& event, const TEventArgs* args) {
    ::Save(Stream, TEventWithArgsPtr{&event, args});
}

void TSerializer<TEventArgs::TArg>::Save(IOutputStream* out, const TEventArgs::TArg& v) {
    // TODO: saveload for TVariant (?)

    ::SaveStr(out, v.Name);

    i8 tag = v.Value.Index();
    ::Save(out, tag);
    Visit(TSaveVisitor{out}, v.Value);
}

void TSerializer<TEventArgs::TArg>::Load(IInputStream* in, TEventArgs::TArg& v, TMemoryPool& pool) {
    using TValue = TEventArgs::TArg::TValue;

    ::LoadStr(in, v.Name, pool);

    i8 tag = 0;
    ::Load(in, tag);
    switch (tag) {
        case TValue::TagOf<TStringBuf>():
            v.Value = TStringBuf();
            ::LoadStr(in, Get<TStringBuf>(v.Value), pool);
            break;

        case TValue::TagOf<i64>():
            v.Value = i64();
            ::Load(in, Get<i64>(v.Value));
            break;

        case TValue::TagOf<double>():
            v.Value = double();
            ::Load(in, Get<double>(v.Value));
            break;

        default:
            ythrow TSerializeException() << "Invalid variant tag: " << tag;
    }
}

void TSerializer<TEventArgs>::Save(IOutputStream* out, const TEventArgs& v) {
    // TODO: saveload for TStackVec

    ::SaveSize(out, v.Items.size());
    ::SaveRange(out, v.Items.begin(), v.Items.end());
}

void TSerializer<TEventArgs>::Load(IInputStream* in, TEventArgs& v, TMemoryPool& pool) {
    ::LoadSizeAndResize(in, v.Items);
    ::LoadRange(in, v.Items.begin(), v.Items.end(), pool);
}

void TSerializer<TDurationBeginEvent>::Save(IOutputStream* out, const TDurationBeginEvent& v) {
    ::SaveMany(out, v.Origin, v.Time, v.Flow);
    ::SaveStr(out, v.Name);
    ::SaveStr(out, v.Categories);
}

void TSerializer<TDurationBeginEvent>::Load(IInputStream* in, TDurationBeginEvent& v, TMemoryPool& pool) {
    ::LoadMany(in, v.Origin, v.Time, v.Flow);
    ::LoadStr(in, v.Name, pool);
    ::LoadStr(in, v.Categories, pool);
}

void TSerializer<TDurationCompleteEvent>::Save(IOutputStream* out, const TDurationCompleteEvent& v) {
    ::SaveMany(out, v.Origin, v.BeginTime, v.EndTime, v.Flow);
    ::SaveStr(out, v.Name);
    ::SaveStr(out, v.Categories);
}

void TSerializer<TDurationCompleteEvent>::Load(IInputStream* in, TDurationCompleteEvent& v, TMemoryPool& pool) {
    ::LoadMany(in, v.Origin, v.BeginTime, v.EndTime, v.Flow);
    ::LoadStr(in, v.Name, pool);
    ::LoadStr(in, v.Categories, pool);
}

void TSerializer<TInstantEvent>::Save(IOutputStream* out, const TInstantEvent& v) {
    ::SaveMany(out, v.Origin, v.Time, v.Scope);
    ::SaveStr(out, v.Name);
    ::SaveStr(out, v.Categories);
}

void TSerializer<TInstantEvent>::Load(IInputStream* in, TInstantEvent& v, TMemoryPool& pool) {
    ::LoadMany(in, v.Origin, v.Time, v.Scope);
    ::LoadStr(in, v.Name, pool);
    ::LoadStr(in, v.Categories, pool);
}

void TSerializer<TAsyncEvent>::Save(IOutputStream* out, const TAsyncEvent& v) {
    ::SaveMany(out, v.SubType, v.Origin, v.Time, v.Id);
    ::SaveStr(out, v.Name);
    ::SaveStr(out, v.Categories);
}

void TSerializer<TAsyncEvent>::Load(IInputStream* in, TAsyncEvent& v, TMemoryPool& pool) {
    ::LoadMany(in, v.SubType, v.Origin, v.Time, v.Id);
    ::LoadStr(in, v.Name, pool);
    ::LoadStr(in, v.Categories, pool);
}

void TSerializer<TCounterEvent>::Save(IOutputStream* out, const TCounterEvent& v) {
    ::SaveMany(out, v.Origin, v.Time);
    ::SaveStr(out, v.Name);
    ::SaveStr(out, v.Categories);
}

void TSerializer<TCounterEvent>::Load(IInputStream* in, TCounterEvent& v, TMemoryPool& pool) {
    ::LoadMany(in, v.Origin, v.Time);
    ::LoadStr(in, v.Name, pool);
    ::LoadStr(in, v.Categories, pool);
}

void TSerializer<TMetadataEvent>::Save(IOutputStream* out, const TMetadataEvent& v) {
    ::Save(out, v.Origin);
    ::SaveStr(out, v.Name);
}

void TSerializer<TMetadataEvent>::Load(IInputStream* in, TMetadataEvent& v, TMemoryPool& pool) {
    ::Load(in, v.Origin);
    ::LoadStr(in, v.Name, pool);
}

void TSerializer<TEventWithArgs>::Save(IOutputStream* out, const TEventWithArgs& v) {
    Visit(TSaveAnyEventVisitor{out, &v.Args}, v.Event);
}

void TSerializer<TEventWithArgs>::Load(IInputStream* in, TEventWithArgs& v, TMemoryPool& pool) {
    i8 tag = 0;
    ::Load(in, tag);
    switch (tag) {
#define CASE(type)                            \
    case TAnyEvent::TagOf<type>():            \
        v.Event = type();                     \
        ::Load(in, Get<type>(v.Event), pool); \
        break;

        CASE(TDurationBeginEvent)
        CASE(TDurationEndEvent)
        CASE(TDurationCompleteEvent)
        CASE(TInstantEvent)
        CASE(TAsyncEvent)
        CASE(TCounterEvent)
        CASE(TMetadataEvent)

#undef CASE

        default:
            ythrow TSerializeException() << "Invalid variant tag: " << tag;
    }
    ::Load(in, v.Args, pool);
}
