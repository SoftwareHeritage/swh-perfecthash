/*
  Copyright (C) 2025  The Software Heritage developers
  See the AUTHORS file at the top-level directory of this distribution
  License: GNU General Public License version 3, or any later version
  See top-level LICENSE file for more information
*/

#include "shard.h"
#include <cerrno>
#include <climits>
#include <cstring>
#include <pybind11/pybind11.h>
#include <pybind11/pytypes.h>
#include <pybind11/stl.h>

namespace py = pybind11;

using namespace std::string_literals;

class ShardCreator {
  public:
    ShardCreator(const std::string &path, uint64_t n)
        : n_entries(n), n_registered(0) {
        this->shard = shard_init(path.c_str());
    }
    ~ShardCreator() { shard_destroy(this->shard); }
    void write(const py::bytes key, const py::bytes object) {
        if (n_registered >= n_entries) {
            throw py::value_error(
                "The declared number of objects has already been written");
        }
        std::string_view kbuf = key;
        if (kbuf.size() != SHARD_KEY_LEN) {
            throw std::length_error(
                "Invalid key size: "s + std::to_string(kbuf.size()) +
                " (expected: " + std::to_string(SHARD_KEY_LEN) + ")");
        }
        std::string_view sv = object;
        errno = 0;
        if (shard_object_write(this->shard, kbuf.data(), sv.data(),
                               sv.size()) != 0) {
            PyErr_SetFromErrno(PyExc_OSError);
            throw py::error_already_set();
        }
        n_registered++;
    }
    ShardCreator &enter() {
        errno = 0;
        if (shard_prepare(this->shard, n_entries) != 0) {
            if (errno != 0) {
                PyErr_SetFromErrno(PyExc_OSError);
                throw py::error_already_set();
            } else
                throw std::runtime_error("shard prepare failed");
        }
        return *this;
    }
    void exit() {
        errno = 0;
        if (n_registered < n_entries) {
            PyErr_SetString(
                PyExc_RuntimeError,
                "The number of registered objects is less than the declared "
                "number of entries; this is not allowed.");
            throw py::error_already_set();
        }
        if (shard_finalize(this->shard) < 0) {
            if (errno == 0) {
                PyErr_SetString(PyExc_RuntimeError,
                                "shard_finalize failed. Was there a duplicate "
                                "key by any chance?");
                throw py::error_already_set();
            } else {
                PyErr_SetFromErrno(PyExc_OSError);
                throw py::error_already_set();
            }
        }
        if (shard_close(this->shard) < 0) {
            PyErr_SetFromErrno(PyExc_OSError);
            throw py::error_already_set();
        }
    }
    shard_t *shard;
    uint64_t n_entries;
    uint64_t n_registered;
};

class ShardReader {
  public:
    ShardReader(const std::string &path) {
        this->shard = shard_init(path.c_str());
        errno = 0;
        if (shard_load(this->shard) != 0) {
            PyErr_SetFromErrno(PyExc_OSError);
            throw py::error_already_set();
        }
    }
    ~ShardReader() {
        // beware the close method (shard_close actually) may fail (not sure
        // how) and this is not captured here... (cannot throw an exception
        // from the destructor in c++17)
        close();
        shard_destroy(this->shard);
        this->shard = NULL;
    }
    int close() {
        errno = 0;
        int ret = shard_close(this->shard);
        return ret;
    }
    py::bytes getitem(const py::bytes key) {
        // get size and position file descriptor at the beginning of the object
        uint64_t size = getsize(key);
        if (size > (uint64_t)SSIZE_MAX) {
            PyErr_SetString(PyExc_ValueError,
                            "Object size overflows python bytes max size "
                            "(are you still using a 32bits system?)");
            throw py::error_already_set();
        }
        ssize_t bufsize = size;
        // instantiate a py::bytes of required size
        py::bytes b = py::bytes(NULL, bufsize);
        // string_view.data() returns a const pointer, so enforce the cast to a
        // char* (yep, that's not nice...)
        char *buf = (char *)std::string_view(b).data();
        if (shard_read_object(this->shard, buf, size) != 0)
            throw std::runtime_error(
                "Content read failed. Shard file might be corrupted.");
        return b;
    }
    uint64_t getpos(const py::bytes key) {
        uint64_t pos;
        std::string kbuf = std::string(key);
        if (kbuf.size() != SHARD_KEY_LEN) {
            throw std::length_error(
                "Invalid key size: "s + std::to_string(kbuf.size()) +
                " (expected: " + std::to_string(SHARD_KEY_LEN) + ")");
        }
        shard_cmph_search(this->shard, kbuf.data(), &pos);
        return pos;
    }
    void getindex(uint64_t pos, shard_index_t &idx) {
        if (shard_index_get(this->shard, pos, &idx) < 0) {
            if (errno != 0)
                PyErr_SetFromErrno(PyExc_OSError);
            else
                PyErr_SetString(
                    PyExc_ValueError,
                    "Cannot retrieve index; either the asked position is "
                    "out range or the index cannot be found.");

            throw py::error_already_set();
        }
    }
    uint64_t getsize(const py::bytes key) {
        std::string kbuf = std::string(key);
        if (kbuf.size() != SHARD_KEY_LEN) {
            throw std::length_error(
                "Invalid key size: "s + std::to_string(kbuf.size()) +
                " (expected: " + std::to_string(SHARD_KEY_LEN) + ")");
        }
        uint64_t size;
        if (shard_find_object(this->shard, kbuf.data(), &size) != 0)
            throw py::key_error("key not found");
        return size;
    }
    shard_t *shard;
};

PYBIND11_MODULE(_shard, m) {
    py::class_<ShardCreator>(m, "ShardCreator")
        .def(py::init<const std::string &, uint64_t>(),
             "Instantiate a ShardCreator object\n\n"
             "Arguments:\n"
             "  path: the filename of the shard file to create\n"
             "  n: number of objects the shard file will store\n"
             "\n"
             "Typical usage will be:\n\n"
             "  with ShardCreate('my.shard', 10) as shard:\n"
             "      for i in range(10):\n"
             "          shard.write(key_i, content_i)\n\n",
             py::arg("path"), py::arg("n"))
        .def_property_readonly("header",
                               [](ShardCreator &s) -> const shard_header_t & {
                                   return s.shard->header;
                               })
        .def("__enter__", &ShardCreator::enter)
        .def("__exit__",
             [](ShardCreator &s, const std::optional<py::type> &exc_type,
                const std::optional<py::object> &exc_value,
                const std::optional<py::object> &traceback) {
                 // TODO: handle exceptions
                 if (!exc_type)
                     s.exit();
             })
        .def("write", &ShardCreator::write,
             "Write a new object in the shard file identified by given key",
             py::arg("key"), py::arg("object"))
        .def_property_readonly_static(
            "key_len", [](py::object) { return SHARD_KEY_LEN; },
            "Size of the keys (in bytes)")
        .doc() = "A shard file creator helper class";

    py::class_<ShardReader>(m, "ShardReader")
        .def_property_readonly_static(
            "key_len", [](py::object /* self */) { return SHARD_KEY_LEN; },
            "Size of the key (in bytes)")
        .def(py::init<const std::string &>(),
             "Instantiate a ShardReader object\n\n"
             "Arguments:\n"
             "  path: the filename of the shard to read\n"
             "\n",
             py::arg("path"))
        .def("close", &ShardReader::close,
             "Close the shard\n\n"
             "Unload the shard index and CMPH data structure and close the "
             "file\n")

        .def_property_readonly("header",
                               [](ShardReader &s) -> const shard_header_t & {
                                   return s.shard->header;
                               })
        .def(
            "getindex",
            [](ShardReader &s, uint64_t pos) -> shard_index_t {
                shard_index_t idx;
                s.getindex(pos, idx);
                return idx;
            },
            "Get the index at position pos\n\n"
            "Arguments:\n"
            "  pos: the position (in the index structure) of the index entry "
            "to retrieve\n\n"
            "The returned index structure may have a NULL key (0x000000...00) "
            "if there is no stored object for the index entry at given "
            "position\n\n",
            py::arg("pos"))
        .def("getsize", &ShardReader::getsize,
             "Get the size of the object from the given key\n\n"
             "Arguments:\n"
             "  key: the key of the object to look for\n"
             "\n"
             "If the key is not found in the shard file, throw a KeyError "
             "exception.\n",
             py::arg("key"))
        .def("getpos", &ShardReader::getpos,
             "Get the index position for the given key\n\n"
             "Arguments:\n"
             "  key: the key of the object to look for\n"
             "\n"
             "If the key is not found in the shard file, throw a KeyError "
             "exception.\n",
             py::arg("key"))
        .def_static(
            "delete",
            [](const std::string &path, const py::bytes key) {
                std::string_view kbuf = key;
                if (kbuf.size() != SHARD_KEY_LEN) {
                    throw std::length_error(
                        "Invalid key size: "s + std::to_string(kbuf.size()) +
                        " (expected: " + std::to_string(SHARD_KEY_LEN) + ")");
                }
                ShardReader reader(path);
                shard_delete(reader.shard, kbuf.data());
            },
            "Remove an entry from the given shard file\n\n"
            "Arguments:\n"
            "  path: the filename of the shard to read\n"
            "  key: the key of the object to delete\n"
            "\n"
            "If the key is not found in the shard file, throw a KeyError "
            "exception.\n\n"
            "Note: this is a static method:\n\n"
            "  ShardReader.delete(shard_filename, key)\n"
            "\n",
            py::arg("path"), py::arg("key"))
        .def(
            "find",
            [](ShardReader &s, const py::bytes key) {
                std::string_view kbuf = key;
                if (kbuf.size() != SHARD_KEY_LEN) {
                    throw std::length_error(
                        "Invalid key size: "s + std::to_string(kbuf.size()) +
                        " (expected: " + std::to_string(SHARD_KEY_LEN) + ")");
                }
                uint64_t size;
                if (shard_find_object(s.shard, kbuf.data(), &size) != 0)
                    throw py::key_error("key not found");
                return size;
            },
            "Look for an object in the shard file from its key\n\n"
            "Arguments:\n"
            "  key: the key to look for\n"
            "Returns:\n"
            "  the size of the object if found\n"
            "\n",
            py::arg("key"))
        .def("__getitem__", &ShardReader::getitem, "See .lookup()\n")
        .def("lookup", &ShardReader::getitem,
             "Return the object from the shard file at given key\n\n"
             "Arguments:\n"
             "  key: the key of the object to look for\n"
             "Returns:\n"
             "  the object content as a bytes\n"
             "Raises:\n"
             "  KeyError: if the key is not found in the shard file\n"
             "  ValueError: if the object size is too big\n"
             "  RuntimeError: if the content could not be read (IO error)\n"
             "\n",
             py::arg("key"))
        .doc() =
        ("A shard file reader helper class\n\n"
         "This allows to easily read a shord file, get information (header) "
         "and extract objects from the shard file. Also provides a static "
         "method "
         "allowing to modify a shard file to delete an object.");

    py::class_<shard_header_t>(m, "ShardHeader")
        .def_readonly("version", &shard_header_t::version,
                      "Version of shard file format")
        .def_readonly("objects_count", &shard_header_t::objects_count,
                      "Number of objects in this shard (possibly including "
                      "deleted objects)")
        .def_readonly("objects_position", &shard_header_t::objects_position,
                      "Position in the file of the Objects section")
        .def_readonly("objects_size", &shard_header_t::objects_size,
                      "Total size of the Objects section")
        .def_readonly("index_position", &shard_header_t::index_position,
                      "Position in the shard file of the Index section")
        .def_readonly("index_size", &shard_header_t::index_size,
                      "Totale size of the Index section")
        .def_readonly(
            "hash_position", &shard_header_t::hash_position,
            "Position in the shard file of the HashMapFunction section")
        .doc() = "Structure for the header of a shard file";

    py::class_<shard_index_t>(m, "ShardIndex")
        .def_property_readonly(
            "key",
            [](shard_index_t &s) -> py::bytes {
                return py::bytes(s.key, SHARD_KEY_LEN);
            },
            "Key of the content object")
        .def_readonly("object_offset", &shard_index_t::object_offset,
                      "Offset of object (absolute offset, should be in the "
                      "Objects section)")
        .doc() = "Structure of an Index element";
};
