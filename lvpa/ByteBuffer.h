#ifndef BYTEBUFFER_H
#define BYTEBUFFER_H

#include "LVPACommon.h"
#include "ByteConverter.h"

#include <string.h> // for memcpy
#include <assert.h>


LVPA_NAMESPACE_START


#define BB_MAKE_WRITE_OP(T) inline ByteBuffer& operator<<(T val) { append<T>(val); return *this; }
#define BB_MAKE_READ_OP(T) inline ByteBuffer& operator>>(T &val) { val = read<T>(); return *this; }
       
class ByteBuffer
{
public:
    typedef void (*delete_func)(void*);
    typedef void *(*allocator_func)(size_t);

    enum Mode // for creation with existing pointers
    {
        COPY,  //- Make a copy of the buffer (default action).
        REUSE,   //- Use the passed-in buffer as is.  Requires the pointer
        //  to remain valid over the life of this object.
        TAKE_OVER, //- Take over the passed-in buffer; it will be deleted on object destruction.
    };

    class Exception
    {
    public:
        Exception(const ByteBuffer *bb, const char *act, uint32 sp = 0)
        {
            action = act;
            rpos = bb->rpos();
            wpos = bb->wpos();
            sizeparam = sp;
            cursize = bb->size();
        }
        uint32 rpos, wpos, sizeparam, cursize;
        const char *action;
    };

#ifdef BYTEBUFFER_NO_EXCEPTIONS
#define BYTEBUFFER_EXCEPT(bb, desc, sz) { Exception __e(bb, desc, sz); \
    fprintf(stderr, "Exception in ByteBuffer: '%s', rpos: %u, wpos: %u, cursize: %u, sizeparam: %u", \
    __e.action, __e.rpos, __e.wpos, __e.cursize, __e.sizeparam); abort(); }
#else
#define BYTEBUFFER_EXCEPT(bb, desc, sz) throw Exception(bb, desc, sz)
#endif

protected:

    uint8 *_buf; // the ptr to the buffer that holds all the bytes
    uint32 _rpos, // read position, [0 ... _size]
        _wpos, // write position, [0 ... _size]
        _res,  // reserved buffer size, [0 ... _size ... _res]
        _size; // used buffer size
    delete_func _delfunc;
    allocator_func _allocfunc;
    bool _mybuf; // if true, destructor deletes buffer
    bool _growable; // default true, if false, buffer will not re-allocate more space

public:


    ByteBuffer()
        : _rpos(0), _wpos(0), _buf(NULL), _size(0), _growable(true), _res(0), _mybuf(false), _delfunc(NULL),
        _allocfunc(NULL)
    {
    }
    ByteBuffer(uint32 res)
        : _rpos(0), _wpos(0), _buf(NULL), _size(0), _growable(true), _res(0), _mybuf(false), _delfunc(NULL),
        _allocfunc(NULL)
    {
        _allocate(res);
    }
    ByteBuffer(ByteBuffer &buf, Mode mode = COPY, uint32 extra = 0)
        : _rpos(0), _wpos(0), _buf(NULL), _size(0), _growable(true), _res(0), _mybuf(false), _delfunc(NULL),
        _allocfunc(NULL)
    {
        init(buf, mode, extra);
    }
    // del param only used with TAKE_OVER, extra only used with COPY
    ByteBuffer(void *buf, uint32 size, Mode mode = COPY, delete_func del = NULL, uint32 extra = 0)
        : _rpos(0), _wpos(0), _size(size), _buf(NULL), _growable(true), _delfunc(del),
        _mybuf(false), _allocfunc(NULL) // for mode == REUSE
    {
        init(buf, size, mode, del, extra);
    }

    void init(void *buf, uint32 size, Mode mode = COPY, delete_func del = NULL, uint32 extra = 0)
    {
        _mybuf = false;
        switch(mode)
        {
        case COPY:
            _allocate(size + extra);
            append(buf, size);
            break;

        case TAKE_OVER:
            _mybuf = true; // fallthrough
        case REUSE:
            _buf = (uint8*)buf;
            _res = size;
            _size = size;
        }
    }

    void init(ByteBuffer& bb, Mode mode = COPY, uint32 extra = 0)
    {
        _allocfunc = bb._allocfunc;

        switch(mode)
        {
        case COPY:
            reserve(bb.size() + extra);
            append(bb);
            break;

        case TAKE_OVER:
        case REUSE:
            _mybuf = bb._mybuf;
            _delfunc = bb._delfunc;
            _buf = bb._buf;
            _res = bb._res;
            _size = bb._size;
            _growable = bb._growable;
            break;
        }

        if(mode == TAKE_OVER)
        {
            bb._buf = NULL;
            bb._size = 0;
            bb._res = 0;
        }
    }

    virtual ~ByteBuffer()
    {
        clear();
    }

    void clear(void)
    {
        _delete();
        reset();
    }

    inline void reset(void)
    {
        _rpos = _wpos = _size = 0;
    }

    void resize(uint32 newsize)
    {
        reserve(newsize);
        _rpos = 0;
        _wpos = newsize;
        _size = newsize;
    }

    void reserve(uint32 newsize)
    {
        if(_res < newsize)
            _allocate(newsize);
    }

    // ---------------------- Write methods -----------------------

    BB_MAKE_WRITE_OP(char);
    BB_MAKE_WRITE_OP(uint8);
    BB_MAKE_WRITE_OP(uint16);
    BB_MAKE_WRITE_OP(uint32);
    BB_MAKE_WRITE_OP(uint64);
    BB_MAKE_WRITE_OP(float);
    BB_MAKE_WRITE_OP(double);
    BB_MAKE_WRITE_OP(int);

    ByteBuffer &operator<<(bool value)
    {
        append<char>((char)value);
        return *this;
    }

    ByteBuffer &operator<<(const char *str)
    {
        append((uint8 *)str, str ? strlen(str) : 0);
        append((uint8)0);
        return *this;
    }

    ByteBuffer &operator<<(const std::string &value)
    {
        append((uint8 *)value.c_str(), value.length());
        append((uint8)0);
        return *this;
    }

    // -------------------- Read methods --------------------

    BB_MAKE_READ_OP(char);
    BB_MAKE_READ_OP(uint8);
    BB_MAKE_READ_OP(uint16);
    BB_MAKE_READ_OP(uint32);
    BB_MAKE_READ_OP(uint64);
    BB_MAKE_READ_OP(float);
    BB_MAKE_READ_OP(double);
    BB_MAKE_READ_OP(int);

    ByteBuffer &operator>>(bool &value)
    {
        value = read<char>() > 0 ? true : false;
        return *this;
    }

    uint8 operator[](uint32 pos)
    {
        return read<uint8>(pos);
    }

    ByteBuffer &operator>>(std::string& value)
    {
        value.clear();
        char c;
        while(readable() && (c = read<char>()))
            value += c;
        return *this;
    }

    // --------------------------------------------------

    uint32 rpos() const { return _rpos; }
    uint32 rpos(uint32 rpos)
    {
        _rpos = rpos < size() ? rpos : size();
        return _rpos;
    }

    uint32 wpos() const { return _wpos; }
    uint32 wpos(uint32 wpos)
    {
        _wpos = wpos < size() ? wpos : size();
        return _wpos;
    }

    template <typename T> T read()
    {
        T r = read<T>(_rpos);
        _rpos += sizeof(T);
        return r;
    }
    template <typename T> T read(uint32 pos) const
    {
        if(pos + sizeof(T) > size())
            BYTEBUFFER_EXCEPT(this, "read", sizeof(T));
        T val = *((T const*)(_buf + pos));
        ToLittleEndian<T>(val);
        return val;
    }

    void read(void *dest, uint32 len)
    {
        if (_rpos + len <= size())
            memcpy(dest, &_buf[_rpos], len);
        else
            BYTEBUFFER_EXCEPT(this, "read-into", len);
        _rpos += len;
    }

    void skipRead(uint32 len)
    {
        _rpos += len;
    }

    inline const uint8 *contents() const { return _buf; }
    inline       uint8 *contents()       { return _buf; }

    inline const void *ptr() const { return _buf; }
    inline       void *ptr()       { return _buf; }

    inline uint32 size() const { return _size; }

    inline uint32 bytes() const { return size(); }
    inline uint32 bits() const { return bytes() * 8; }

    inline uint32 capacity() const { return _res; }

    inline uint32 readable(void) const { return size() - rpos(); }
    inline uint32 writable(void) const { return size() - wpos(); } // free space left before realloc will occur

    template <typename T> void append(T value)
    {
        ToLittleEndian<T>(value);
        _enlargeIfReq(_wpos + sizeof(T));
        *((T*)(_buf + _wpos)) = value;
        _wpos += sizeof(T);
        if(_size < _wpos)
            _size = _wpos;
    }

    void append(const void *src, uint32 bytes)
    {
        if (!bytes) return;
        _enlargeIfReq(_wpos + bytes);
        memcpy(_buf + _wpos, src, bytes);
        _wpos += bytes;
        if(_size < _wpos)
            _size = _wpos;
    }
    void append(const ByteBuffer& buffer)
    {
        if(buffer.size())
            append(buffer.contents(), buffer.size());
    }

    void put(uint32 pos, const void *src, uint32 bytes)
    {
        memcpy(_buf + pos, src, bytes);
    }

    template <typename T> void put(uint32 pos, T value)
    {
        if(pos >= size())
            BYTEBUFFER_EXCEPT(this, "put", sizeof(T));

        ToLittleEndian<T>(value);
        *((T*)(_buf + pos)) = value;
    }

    inline bool growable(void) { return _growable; }
    inline void growable(bool b) { _growable = b; }

    // dangerous functions

    void _setPtr(void *p)
    {
        _buf = (uint8*)p;
    }

    void _setAllocFunc(allocator_func f)
    {
        _allocfunc = f;
    }

    void _setDelFunc(delete_func f)
    {
        _delfunc = f;
    }

    void _setSize(uint32 s)
    {
        _size = s;
    }

    void _setReserved(uint32 s)
    {
        _res = s;
    }

protected:

    void _delete(void)
    {
        if(_mybuf)
        {
            if(_delfunc)
                _delfunc(_buf);
            else
                delete [] _buf;
            _buf = NULL;
            _res = 0;
        }
    }

    // allocate larger buffer and copy contents. if we own the current buffer, delete old, otherwise, leave it as it is.
    void _allocate(uint32 s)
    {
        if(!_growable && _buf) // only throw if we already have a buf
            BYTEBUFFER_EXCEPT(this, "_alloc+locked", s);

        // dangerous: It's up to the user to be sure that _allocfunc and _delfunc are matching
        uint8 *newbuf = (uint8*)(_allocfunc ? _allocfunc(s) : new char[s]);
        if(_buf)
        {
            memcpy(newbuf, _buf, _size);
            _delete();
        }
        _buf = newbuf;
        _res = s;
        _mybuf = true;

        if (!_allocfunc)
            _delfunc = NULL;
    }

    void _enlargeIfReq(uint32 minSize)
    {
        if(_res < minSize)
        {
            uint32 a = _res * 2;
            if(a < minSize) // fallback if doubling the space was not enough
                a += minSize;
            _allocate(a);
        }
    }


};


#undef BB_MAKE_WRITE_OP
#undef BB_MAKE_READ_OP


LVPA_NAMESPACE_END


#endif
