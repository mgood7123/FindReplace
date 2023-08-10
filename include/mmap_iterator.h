#include <mmap.h>

#include <iterator>

#define SATISFIES__DEFAULT_CONSTRUCTIBLE(C) C();

#define SATISFIES__MOVE_CONSTRUCTIBLE(C) C(C && other);
#define SATISFIES__MOVE_ASSIGNABLE(C) C & operator=(C && other);

#define SATISFIES__COPY_CONSTRUCTIBLE(C); \
    SATISFIES__MOVE_CONSTRUCTIBLE(MMapIterator); \
    C(const C & other);

#define SATISFIES__COPY_ASSIGNABLE(C) \
    SATISFIES__MOVE_ASSIGNABLE(MMapIterator); \
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

struct MMapIterator {

    private:
    
    MMapHelper * map = nullptr;
    
    mutable std::shared_ptr<MMapHelper::Page> current_page;

    public:
    using iterator_category = std::bidirectional_iterator_tag;
    using value_type = char;
    using pointer = char *;
    using reference = char &;
    using difference_type = std::ptrdiff_t;

    private:
    mutable difference_type index = 0;

    public:
    MMapIterator(MMapHelper & map, std::size_t index);

    bool is_open() const;
    size_t length() const;
    const char * get_api() const;
    size_t get_page_size() const;

    SATISFIES__LEGACY_BIDIRECTIONAL_ITERATOR(MMapIterator);

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
