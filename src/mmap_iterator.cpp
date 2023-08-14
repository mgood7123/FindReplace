#include <mmap_iterator.h>

static auto up = [](auto & index, auto & current_page, auto & map, auto & page_size) {
    // compute offset from index
    auto s = 0;
    while (!(index < (s+page_size))) {
        s += page_size;
    }
    // index == 0, s = 0, index < page_size, s = 0
    // index == 5, s = 0, index < page_size, s = 0
    // index == page_size, s = 0, ! index < page_size, s = page_size
    // index == page_size, s = page_size, index < (page_size*2), s = page_size
    if (current_page.get() == nullptr) {
        // std::cout << "NULLPTR PAGE: map buffer for new index of " << std::to_string(s) << std::endl;
    } else {
        // std::cout << "map buffer for new index of " << std::to_string(s) << std::endl;
    }
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

MMapIterator::MMapIterator() : page_size(mmaptwo::get_page_size()*400), api(mmaptwo::get_os() == mmaptwo::os_unix ? "mmap(2)" : mmaptwo::get_os() == mmaptwo::os_win32 ? "MapViewOfFile" : "(unknown api)") {
}

MMapIterator::MMapIterator(MMapHelper & map, std::size_t index) : map(&map), index(index), page_size(mmaptwo::get_page_size()), api(mmaptwo::get_os() == mmaptwo::os_unix ? "mmap(2)" : mmaptwo::get_os() == mmaptwo::os_win32 ? "MapViewOfFile" : "(unknown api)") {
    if (this->map->length() <= page_size) {
        auto t = this->map->obtain_map(0, this->map->length());
        if (t.get() == nullptr) {
            throw std::runtime_error("FAILED TO OBTAIN MAPPING");
        }
        current_page = t;
    } else {
        up(index, current_page, this->map, page_size);
    }
}
MMapIterator::MMapIterator(const MMapIterator & other) { page_size = other.page_size; api = other.api; map = other.map; index = other.index; if (other.index != 0 && other.current_page.get() != nullptr) current_page = other.current_page; }
MMapIterator::MMapIterator(MMapIterator && other) { page_size = std::move(other.page_size); api = std::move(other.api); map = std::move(other.map); index = std::move(other.index); if (other.index != 0 && other.current_page.get() != nullptr) current_page = std::move(other.current_page); }
MMapIterator & MMapIterator::operator=(const MMapIterator & other) { page_size = other.page_size; api = other.api; map = other.map; index = other.index; if (other.index != 0 && other.current_page.get() != nullptr) current_page = other.current_page; return *this; }
MMapIterator & MMapIterator::operator=(MMapIterator && other) { page_size = std::move(other.page_size); map = std::move(other.map); api = std::move(other.api); index = std::move(other.index); if (other.index != 0 && other.current_page.get() != nullptr) current_page = std::move(other.current_page); return *this; }
bool MMapIterator::operator==(const MMapIterator & other) const { 
    // std::cout << "mmap_iterator ==   return  (" << std::to_string(index) << " == " << std::to_string(other.index) << ")" << std::endl;
    return map == other.map && index == other.index;
}
bool MMapIterator::operator!=(const MMapIterator & other) const { 
    // std::cout << "mmap_iterator !=   return !(" << std::to_string(index) << " == " << std::to_string(other.index) << ")" << std::endl;
    return !(map == other.map && index == other.index);
}
MMapIterator::~MMapIterator() {}

const char * MMapIterator::get_api() const { return api; }
size_t MMapIterator::get_page_size() const { return page_size; }
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
    If i is incrementable, then both ++i and i++ advance i, and ...
    If i is incrementable, then std::addressof(++i) == std::addressof(i). ```
*/

// ++it
//
MMapIterator & MMapIterator::operator++(int) {
    // std::cout << "mmap_iterator ++, before index " << std::to_string(index) << std::endl;
    index++;
    // std::cout << "mmap_iterator ++, after index " << std::to_string(index) << std::endl;
    return *this;
}

// it++
// Equivalent expression: It ip = i; ++i; return ip;
//
MMapIterator MMapIterator::operator++() {
    MMapIterator copy = *this;
    // std::cout << "mmap_iterator ++, before index " << std::to_string(index) << std::endl;
    index++;
    // std::cout << "mmap_iterator ++, after index " << std::to_string(index) << std::endl;
    return copy;
}

// --it
//
MMapIterator & MMapIterator::operator--(int) {
    // std::cout << "mmap_iterator --, before index " << std::to_string(index) << std::endl;
    index--;
    // std::cout << "mmap_iterator --, after index " << std::to_string(index) << std::endl;
    return *this;
}

// it--
// Equivalent expression: It ip = i; --i; return ip;
//
MMapIterator MMapIterator::operator--() {
    MMapIterator copy = *this;
    // std::cout << "mmap_iterator --, before index " << std::to_string(index) << std::endl;
    index--;
    // std::cout << "mmap_iterator --, after index " << std::to_string(index) << std::endl;
    return copy;
}

MMapIterator::reference MMapIterator::operator*() const {
    auto page = current_page.get();
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
    // std::cout << "mmap_iterator dereference, index " << std::to_string(tmp) << std::endl;
    return reinterpret_cast<char*>(current_page->get())[tmp];
}

MMapIterator::pointer MMapIterator::operator->() const {
    return &this->operator*();
}
