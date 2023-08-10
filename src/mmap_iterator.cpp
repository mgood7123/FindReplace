#include <mmap_iterator.h>

MMapIterator::MMapIterator() {}
MMapIterator::MMapIterator(MMapHelper & map, std::size_t index) : map(&map), index(index) {}
MMapIterator::MMapIterator(const MMapIterator & other) { map = other.map; index = other.index; if (other.index != 0 && other.current_page.get() != nullptr) current_page = other.current_page; }
MMapIterator::MMapIterator(MMapIterator && other) { map = std::move(other.map); index = std::move(other.index); if (other.index != 0 && other.current_page.get() != nullptr) current_page = std::move(other.current_page); }
MMapIterator & MMapIterator::operator=(const MMapIterator & other) { map = other.map; index = other.index; if (other.index != 0 && other.current_page.get() != nullptr) current_page = other.current_page; return *this; }
MMapIterator & MMapIterator::operator=(MMapIterator && other) { map = std::move(other.map); index = std::move(other.index); if (other.index != 0 && other.current_page.get() != nullptr) current_page = std::move(other.current_page); return *this; }
bool MMapIterator::operator==(const MMapIterator & other) const { return map == other.map && index == other.index; }
bool MMapIterator::operator!=(const MMapIterator & other) const { return !(*this == other); }
MMapIterator::~MMapIterator() {}

const char * MMapIterator::get_api() const { return map->get_api(); }
size_t MMapIterator::get_page_size() const { return map->get_page_size(); }
bool MMapIterator::is_open() const { return map->is_open(); }
size_t MMapIterator::length() const { return map->length(); }

/*
`LegacyIterator` specifies
```cpp
Expression: ++r
Return type: It&
```

`LegacyInputIterator` specifies
```cpp
Expression: (void)r++
Return type:
Equivalent expression: (void)++r

Expression: *r++
Return type: convertible to value_type
Equivalent expression: value_type x = *r; r++; return x;
```

`LegacyForwardIterator` specifies
```cpp
Expression: i++
Return type: It
Equivalent expression: It ip = i; ++i; return ip;
```

`LegacyBidirectionalIterator` specifies
```cpp
Expression: i++
Return type: It
Equivalent expression: It ip = i; ++i; return ip;
```

https://en.cppreference.com/w/cpp/iterator/weakly_incrementable specifies
```cpp
I models std::weakly_incrementable only if, for an object i of type I: 
    The expressions ++i and i++ have the same domain,
    If i is incrementable, then both ++i and i++ advance i, and
    If i is incrementable, then std::addressof(++i) == std::addressof(i). ```
*/

// ++it
//
MMapIterator & MMapIterator::operator++(int) {
    index++;
    return *this;
}

// it++
// Equivalent expression: It ip = i; ++i; return ip;
//
MMapIterator MMapIterator::operator++() {
    MMapIterator copy = *this;
    index++;
    return copy;
}

// --it
//
MMapIterator & MMapIterator::operator--(int) {
    index--;
    return *this;
}

// it--
// Equivalent expression: It ip = i; --i; return ip;
//
MMapIterator MMapIterator::operator--() {
    MMapIterator copy = *this;
    index--;
    return copy;
}

auto up = [](auto & index, auto & current_page, auto & map, auto & page_size) {
    // compute offset from index
    auto s = 0;
    while (!(index < (s+page_size))) {
        s += page_size;
    }
    // index == 0, s = 0, index < page_size, s = 0
    // index == 5, s = 0, index < page_size, s = 0
    // index == page_size, s = 0, ! index < page_size, s = page_size
    // index == page_size, s = page_size, index < (page_size*2), s = page_size
    if (map->length() < (s + page_size)) {
        auto t = map->obtain_map(s, map->length() - s);
        if (t.get() == nullptr) {
            throw std::runtime_error("FAILED TO OBTAIN MAPPING");
        }
        current_page = t;
    } else {
        auto t = map->obtain_map(s, page_size);
        if (t.get() == nullptr) {
            throw std::runtime_error("FAILED TO OBTAIN MAPPING");
        }
        current_page = t;
    }
};

MMapIterator::reference MMapIterator::operator*() const {
    auto page = current_page.get();
    auto page_size = map->get_page_size();
    if (page == nullptr) {
        if (map->length() <= page_size) {
            auto t = map->obtain_map(0, map->length());
            if (t.get() == nullptr) {
                throw std::runtime_error("FAILED TO OBTAIN MAPPING");
            }
            current_page = t;
        } else {
            up(index, current_page, map, page_size);
        }
    } else {
        if (map->length() > page_size) {
            auto off = page->offset();
            if (!(index >= off && index < (off + page_size))) {
                up(index, current_page, map, page_size);
            }
        }
    }
    auto tmp = index;
    while (tmp >= page_size) tmp -= page_size;
    // std::cout << "accessing index " << std::to_string(tmp) << " of mapped page: " << current_page << " with offset " << std::to_string(current_page->offset()) << " and length " << std::to_string(current_page->length()) <<  std::endl;
    return reinterpret_cast<char*>(current_page->get())[tmp];
}

MMapIterator::pointer MMapIterator::operator->() const {
    return &this->operator*();
}
