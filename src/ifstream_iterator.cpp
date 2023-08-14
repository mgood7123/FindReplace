#include <ifstream_iterator.h>

const std::size_t ifstream_iterator::chunk = 4096;
static auto shared_buffer = std::make_shared<std::vector<char>>();

ifstream_iterator::ifstream_iterator() : index(0), is_eof(true), stream(nullptr) {}


ifstream_iterator::ifstream_iterator(std::ifstream & stream, std::size_t index) : stream(&stream), index(index), is_eof(false) {
    State copy;
    copy.save(&stream);
    seek(index);
    if (is_eof) {
        stream_state.save(&stream);
        copy.restore(&stream);
        return;
    }
    if (shared_buffer->size() != chunk) {
        // std::cout << "resizing shared buffer" << std::endl;
        shared_buffer->resize(chunk);
    }
    // std::cout << "using shared buffer" << std::endl;
    buffer.buffer = shared_buffer;
    // std::cout << "read buffer for new index of 0" << std::endl;
    auto g = read_buf();
    buffer.buffer_start = buffer.buffer_end;
    buffer.buffer_end += g;
    if (stream.eof()) {
        copy.restore(&stream);
        stream.seekg(buffer.buffer_end);
    }
    stream_state.save(&stream);
    copy.restore(&stream);
}
ifstream_iterator::ifstream_iterator(std::ifstream & stream) : stream(&stream), index(0), is_eof(true) {
    stream_state.save(&stream);
}
ifstream_iterator::ifstream_iterator(const ifstream_iterator & other) = default;
ifstream_iterator::ifstream_iterator(ifstream_iterator && other) = default;
ifstream_iterator & ifstream_iterator::operator=(const ifstream_iterator & other) = default;
ifstream_iterator & ifstream_iterator::operator=(ifstream_iterator && other) = default;
bool ifstream_iterator::operator==(const ifstream_iterator & other) const {
    return do_op(6, &other) == nullptr;
}
bool ifstream_iterator::operator!=(const ifstream_iterator & other) const {
    return do_op(7, &other) == nullptr;
}
ifstream_iterator::~ifstream_iterator() {
}

void ifstream_iterator::State::save(std::ifstream & stream) {
    save(&stream);
}

void ifstream_iterator::State::restore(std::ifstream & stream) {
    restore(&stream);
}

void ifstream_iterator::State::save(std::ifstream * stream) {
    state = stream->rdstate();
    exception = stream->exceptions();
    stream->clear(std::ifstream::goodbit);
    stream->exceptions(std::ifstream::goodbit);
    pos = stream->tellg();
    stream->exceptions(exception);
    stream->clear(state);
}

void ifstream_iterator::State::restore(std::ifstream * stream) {
    stream->clear(std::ifstream::goodbit);
    stream->exceptions(std::ifstream::goodbit);
    stream->seekg(pos);
    stream->exceptions(exception);
    stream->clear(state);
}

void ifstream_iterator::check_eof() {
    check_stream();
    if (stream->eof()) {
        set_eof();
        return;
    } else {
        stream_state.save(stream);
        stream->seekg(index);
        stream->get();
        check_stream();
        if (stream->eof()) set_eof();
        stream->unget();
        stream_state.restore(stream);
    }
}

void ifstream_iterator::set_eof() {
    is_eof = true;
    prev_index = index;
    index = 0;
}

void ifstream_iterator::unset_eof() {
    is_eof = false;
    index = prev_index-1;
}

void ifstream_iterator::check_stream() {
    if (stream->good()) {
    } else if (stream->bad()) {
        std::cout << "I/O error while reading" << std::endl;
        abort();
    } else if (stream->eof()) {
    } else if (stream->fail()) {
        std::cout << "Non-integer data encountered" << std::endl;
        abort();
    } else  {
        std::cout << "stream in unknown state" << std::endl;
        abort();
    }
}

void ifstream_iterator::seek(std::size_t index) {
    stream->seekg(index);
    check_eof();
}

// does not modify the index
std::size_t ifstream_iterator::read_buf() {
    if (is_eof) return 0;
    seek(index);
    if (is_eof) return 0;
    // tellg may modify the bit state
    if (stream->tellg() == -1) {
        check_stream();
        std::cout << "FAILED TO SEEK FILE" << std::endl;
        abort();
    }
    check_stream();
    // std::cout << "read buffer for index " << std::to_string(index) << std::endl;
    if (buffer.buffer.use_count() > 2) {
        // std::cout << "resize buffer for index " << std::to_string(index) << std::endl;
        buffer.buffer = std::make_shared<std::vector<char>>();
        buffer.buffer->resize(chunk);
    } else {
        // std::cout << "reusing shared buffer for index " << std::to_string(index) << std::endl;
    }
    stream->read(buffer.buffer->data(), chunk);
    check_stream();
    // gcount does not modify any bit state
    auto gcount = stream->gcount();
    if (gcount == 0) {
        std::cout << "FAILED TO READ FILE" << std::endl;
        abort();
    }
    return gcount;
}

void ifstream_iterator::seek_buf_forward() {
    if (!is_eof) {
        State copy_before;
        copy_before.save(stream);
        if (index == buffer.buffer_end-1) {
            auto old = index;
            index = buffer.buffer_end;
            auto g = read_buf();
            copy_before.restore(stream);
            if (is_eof) return;
            index = old;
            buffer.buffer_start = buffer.buffer_end;
            buffer.buffer_end += g;
        }
        State copy_after;
        copy_after.save(stream);
        index++;
        // check_eof() does an early exit if stream->eof() returns true
        // this can happen if we manage to fit the rest of the stream within our buffer
        // this however does not mean we have truely reached eof since
        // our actual index itself may not be at the stream EOF index (eg, idx 4, eof 7)
        copy_before.restore(stream);
        seek(index);
        copy_after.restore(stream);
    }
}

void ifstream_iterator::seek_buf_backward() {
    if (is_eof) {
        if (prev_index != 0) {
            unset_eof();
        }
    } else if (index != 0) {
        if (index == buffer.buffer_start) {
            // std::cout << "read buffer for new index of " << std::to_string(index-1) << std::endl;
            buffer.buffer_end = buffer.buffer_start;
            buffer.buffer_start -= chunk;
            seek(buffer.buffer_start);
            read_buf();
        }
        index--;
    }
}

ifstream_iterator & ifstream_iterator::operator++(int) {
    return *static_cast<ifstream_iterator*>(do_op(1));
}

ifstream_iterator ifstream_iterator::operator++() {
    ifstream_iterator copy = *this;
    do_op(2);
    return copy;
}

ifstream_iterator & ifstream_iterator::operator--(int) {
    return *static_cast<ifstream_iterator*>(do_op(3));
}

ifstream_iterator ifstream_iterator::operator--() {
    ifstream_iterator copy = *this;
    do_op(4);
    return copy;
}

// reference and pointer do not make sense because we are buffered input
// and changing buffer content is meaningless
//
// but bidirectional iterator requires such so... kinda give it that ?
// 

ifstream_iterator::reference ifstream_iterator::operator*() const {
    return *static_cast<pointer>(do_op(5, nullptr));
}

ifstream_iterator::pointer ifstream_iterator::operator->() const {
    return static_cast<pointer>(do_op(5, nullptr));
}

void * ifstream_iterator::do_op(int op) {
    if (op == 1) {
        // std::cout << "ifstream_iterator ++, before index " << std::to_string(index) << std::endl;
        stream_state.restore(stream);
        seek_buf_forward();
        stream_state.save(stream);
        // std::cout << "ifstream_iterator ++, after index " << std::to_string(index) << std::endl;
        return this;
    } else if (op == 2) {
        // std::cout << "ifstream_iterator ++, before index " << std::to_string(index) << std::endl;
        stream_state.restore(stream);
        seek_buf_forward();
        stream_state.save(stream);
        // std::cout << "ifstream_iterator ++, after index " << std::to_string(index) << std::endl;
    } else if (op == 3) {
        // std::cout << "ifstream_iterator --, before index " << std::to_string(index) << std::endl;
        stream_state.restore(stream);
        seek_buf_backward();
        stream_state.save(stream);
        // std::cout << "ifstream_iterator --, after index " << std::to_string(index) << std::endl;
        return this;
    } else if (op == 4) {
        // std::cout << "ifstream_iterator --, before index " << std::to_string(index) << std::endl;
        stream_state.restore(stream);
        seek_buf_backward();
        stream_state.save(stream);
        // std::cout << "ifstream_iterator --, after index " << std::to_string(index) << std::endl;
    }
    return nullptr;
}

void * ifstream_iterator::do_op(int op, const void * data) const {
    if (op == 5) {
        // std::cout << "ifstream_iterator dereference, index " << std::to_string(index) << std::endl;
        if (is_eof) {
            throw std::runtime_error("EOF cannot be dereferenced");
        }
        reference ref = const_cast<reference>(buffer.buffer->operator[](index - buffer.buffer_start));
        return &ref;
    } else if (op == 6) {
        const ifstream_iterator & other = * static_cast<const ifstream_iterator*>(data);
        // std::cout << "ifstream_iterator ==   return  (" << std::to_string(index) << " == " << std::to_string(other.index) << " && " << (is_eof ? "EOF" : "NON_EOF") << " == " << (other.is_eof ? "EOF" : "NON_EOF") << ")" << std::endl;
        return stream == other.stream && index == other.index && is_eof == other.is_eof ? nullptr : reinterpret_cast<void*>(0x1);
    } else if (op == 7) {
        const ifstream_iterator & other = * static_cast<const ifstream_iterator*>(data);
        // std::cout << "ifstream_iterator !=   return !(" << std::to_string(index) << " == " << std::to_string(other.index) << " && " << (is_eof ? "EOF" : "NON_EOF") << " == " << (other.is_eof ? "EOF" : "NON_EOF") << ")" << std::endl;
        return stream == other.stream && index == other.index && is_eof == other.is_eof ? reinterpret_cast<void*>(0x1) : nullptr;
    }
    return nullptr;
}
