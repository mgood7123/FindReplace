#include <iterator>
#include <vector>
#include <fstream>
#include <iostream>
#include <memory>

#define SATISFIES__DEFAULT_CONSTRUCTIBLE(C) C();

#define SATISFIES__MOVE_CONSTRUCTIBLE(C) C(C && other);
#define SATISFIES__MOVE_ASSIGNABLE(C) C & operator=(C && other);

#define SATISFIES__COPY_CONSTRUCTIBLE(C); \
    SATISFIES__MOVE_CONSTRUCTIBLE(C); \
    C(const C & other);

#define SATISFIES__COPY_ASSIGNABLE(C) \
    SATISFIES__MOVE_ASSIGNABLE(C); \
    C & operator=(const C & other);

#define SATISFIES__DESTRUCTIBLE(C) virtual ~C();

#define SATISFIES__EQUALITY_COMPARABLE(C) \
    bool operator==(const C & other) const; \
    bool operator!=(const C & other) const;

#define SATISFIES__LEGACY_ITERATOR(C) \
    SATISFIES__COPY_CONSTRUCTIBLE(C) \
    SATISFIES__COPY_ASSIGNABLE(C) \
    SATISFIES__DESTRUCTIBLE(C) \
    reference operator* () const; \
    pointer operator-> () const; \
    C& operator++(int); /* ++it */ \
    C operator++(); /* it++ */

#define SATISFIES__LEGACY_INPUT_ITERATOR(C) \
    SATISFIES__LEGACY_ITERATOR(C) \
    SATISFIES__EQUALITY_COMPARABLE(C) \

#define SATISFIES__LEGACY_FORWARD_ITERATOR(C) \
    SATISFIES__LEGACY_INPUT_ITERATOR(C) \
    SATISFIES__DEFAULT_CONSTRUCTIBLE(C)

// bidirectional iterator required signed difference type
#define SATISFIES__LEGACY_BIDIRECTIONAL_ITERATOR(C) \
    SATISFIES__LEGACY_FORWARD_ITERATOR(C) \
    C& operator--(int); /* --it */ \
    C operator--(); /* it-- */

struct ifstream_iterator {

    public:

    using iterator_category = std::bidirectional_iterator_tag;
    using value_type = char;
    using pointer = char *;
    using reference = char &;
    using difference_type = std::ptrdiff_t;

    struct State {
        std::ifstream::iostate state = std::ifstream::goodbit;
        std::ifstream::iostate exception = std::ifstream::goodbit;
        std::ifstream::pos_type pos = 0;
        void save(std::ifstream * stream);
        void save(std::ifstream & stream);
        void restore(std::ifstream * stream);
        void restore(std::ifstream & stream);
    };

    private:

    template <typename T>
    struct LargeBuffer {
        std::shared_ptr<std::vector<T>> buffer;
        difference_type buffer_start = 0;
        difference_type buffer_end = 0;
        LargeBuffer() {
            buffer = std::make_shared<std::vector<T>>();
        }
        LargeBuffer(const LargeBuffer & other) {
            buffer = other.buffer;
            buffer_start = other.buffer_start;
            buffer_end = other.buffer_end;
        }
        LargeBuffer(LargeBuffer && other) {
            buffer = std::move(other.buffer);
            buffer_start = other.buffer_start;
            buffer_end = other.buffer_end;
        }
        LargeBuffer operator=(const LargeBuffer & other) {
            buffer = other.buffer;
            buffer_start = other.buffer_start;
            buffer_end = other.buffer_end;
            return *this;
        }
        LargeBuffer operator=(LargeBuffer && other) {
            buffer = std::move(other.buffer);
            buffer_start = other.buffer_start;
            buffer_end = other.buffer_end;
            return *this;
        }
    };

    LargeBuffer<char> buffer;

    static const std::size_t chunk;

    std::ifstream * stream = nullptr;
    mutable difference_type index = 0;
    mutable difference_type prev_index = 0;

    bool is_eof = true;

    State stream_state;

    void seek_buf_forward();
    void seek_buf_backward();
    std::size_t read_buf();
    void seek(std::size_t index);
    void check_eof();
    void set_eof();
    void unset_eof();
    void check_stream();
    void * do_op(int op);
    void * do_op(int op, const void * data) const;

    public:
    ifstream_iterator(std::ifstream & stream, std::size_t index);
    ifstream_iterator(std::ifstream & stream);
    SATISFIES__LEGACY_BIDIRECTIONAL_ITERATOR(ifstream_iterator);

};

#undef SATISFIES__COPY_CONSTRUCTIBLE
#undef SATISFIES__COPY_ASSIGNABLE
#undef SATISFIES__MOVE_CONSTRUCTIBLE
#undef SATISFIES__MOVE_ASSIGNABLE
#undef SATISFIES__DESTRUCTIBLE
#undef SATISFIES__DEFAULT_CONSTRUCTIBLE
#undef SATISFIES__EQUALITY_COMPARABLE
#undef SATISFIES__LEGACY_ITERATOR
#undef SATISFIES__LEGACY_INPUT_ITERATOR
#undef SATISFIES__LEGACY_FORWARD_ITERATOR
#undef SATISFIES__LEGACY_BIDIRECTIONAL_ITERATOR
