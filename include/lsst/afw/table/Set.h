// -*- lsst-c++ -*-
#ifndef AFW_TABLE_Set_h_INCLUDED
#define AFW_TABLE_Set_h_INCLUDED

#include <map>
#include <string>

#include "boost/iterator/iterator_adaptor.hpp"
#include "boost/lexical_cast.hpp"

#include "lsst/base.h"
#include "lsst/pex/exceptions.h"
#include "lsst/afw/table/BaseTable.h"
#include "lsst/afw/table/BaseRecord.h"
#include "lsst/afw/table/ColumnView.h"
#include "lsst/afw/table/io/FitsWriter.h"
#include "lsst/afw/table/io/FitsReader.h"
#include "lsst/afw/table/Vector.h"

namespace lsst { namespace afw { namespace table {

template <typename BaseT>
class SetIterator : public boost::iterator_adaptor<SetIterator<BaseT>, BaseT,
                                                   typename BaseT::value_type::second_type::element_type>
{
public:

    typedef typename BaseT::value_type::second_type::element_type pointer;

    SetIterator() {}

    template <typename OtherT>
    SetIterator(SetIterator<OtherT> const & other) : SetIterator::iterator_adaptor_(other.base()) {}

    explicit SetIterator(BaseT const & base) : SetIterator::iterator_adaptor_(base) {}

    template <typename RecordT>
    operator PTR(RecordT) () const { return this->base()->second; }

    template <typename RecordT>
    SetIterator & operator=(PTR(RecordT) const & other) const {
        if (other->getTable() != dereference().getTable()) {
            throw LSST_EXCEPT(
                lsst::pex::exceptions::LogicErrorException,
                "Record to assign must be associated with the container's table."
            );
        }
        this->base()->second = other;
        return *this;
    }

private:
    friend class boost::iterator_core_access;
    typename BaseT::value_type::second_type::element_type & dereference() const {
        return *this->base()->second;
    }
};

template <typename RecordT, typename TableT, typename KeyT, typename CompareT>
class SetT {
    typedef std::map<KeyT,PTR(RecordT),CompareT> Internal;
public:

    typedef RecordT Record;
    typedef TableT Table;

    typedef KeyT key_type;
    typedef CompareT key_compare;
    typedef RecordT value_type;
    typedef RecordT & reference;
    typedef PTR(RecordT) pointer;
    typedef typename Internal::size_type size_type;
    typedef typename Internal::difference_type difference_type;
    typedef SetIterator<typename Internal::iterator> iterator;
    typedef SetIterator<typename Internal::const_iterator> const_iterator;

    PTR(TableT) getTable() const { return _table; }

    Schema getSchema() const { return _table->getSchema(); }

    SetT(PTR(TableT) const & table, Key<KeyT> const & key, CompareT const & compare = CompareT()) :
        _key(key), _table(table), _internal(compare)
    {}

    SetT(Schema const & schema, Key<KeyT> const & key, CompareT const & compare = CompareT()) :
        _key(key), _table(TableT::make(schema)), _internal(compare)
    {}

    template <typename InputIterator>
    SetT(
        PTR(TableT) const & table, Key<KeyT> const & key,
        InputIterator first, InputIterator last,
        bool deep = false, CompareT const & compare = CompareT()
    ) : _key(key), _table(table), _internal(compare)
    {
        insert(this->end(), first, last, deep);
    } 

    SetT(SetT const & other) : _key(other._key), _table(other._table), _internal(other._internal) {}

    SetT & operator=(SetT const & other) {
        if (&other != this) {
            _key = other._key;
            _table = other._table;
            _internal = other._internal;
        }
        return *this;
    }

    void writeFits(std::string const & filename) const {
        io::FitsWriter::apply(filename, *this);
    }

    static SetT readFits(std::string const & filename) {
        return io::FitsReader::apply<SetT>(filename);
    }

    ColumnView getColumnView() const { return ColumnView::make(begin(), end()); }

    iterator begin() { return iterator(_internal.begin()); }
    iterator end() { return iterator(_internal.end()); }

    const_iterator begin() const { return const_iterator(_internal.begin()); }
    const_iterator end() const { return const_iterator(_internal.end()); }

    bool empty() const { return _internal.empty(); }
    size_type size() const { return _internal.size(); }
    size_type max_size() const { return _internal.max_size(); }

    reference operator[](key_type k) const {
        typename Internal::const_iterator i = _internal.find(k);
        if (i == _internal.end()) {
            throw LSST_EXCEPT(
                lsst::pex::exceptions::NotFoundException,
                "Record with key '" + boost::lexical_cast<std::string>(k) + "' not found in Set."
            );
        }
        return *i->second;
    }

    PTR(RecordT) const get(key_type k) const {
        PTR(RecordT) p;
        typename Internal::const_iterator i = _internal.find(k);
        if (i != _internal.end()) p = i->second;
        return p;
    }

    PTR(RecordT) addNew() {
        return insert(_table->makeRecord());
    }

    iterator insert(Record const & r) {
        key_type k = r.get(_key);
        PTR(RecordT) p;
        std::pair<typename Internal::iterator, bool> t = _internal.insert(std::make_pair(k, p));
        if (!t.second) {
            throw LSST_EXCEPT(
                lsst::pex::exceptions::NotFoundException,
                "Record with key '" + boost::lexical_cast<std::string>(k) + "' already present in Set."
            );
        }
        *t.first->second = _table->copyRecord(r);
        return iterator(t.first);
    }

    iterator insert(PTR(RecordT) const & p) {
        if (p->getTable() != _table) {
            throw LSST_EXCEPT(
                lsst::pex::exceptions::LogicErrorException,
                "Record to insert must be associated with the container's table."
            );
        }
        key_type k = p->get(_key);
        std::pair<typename Internal::iterator, bool> t = _internal.insert(std::make_pair(k, p));
        if (!t.second) {
            throw LSST_EXCEPT(
                lsst::pex::exceptions::InvalidParameterException,
                "Record with key '" + boost::lexical_cast<std::string>(k) + "' already present in Set."
            );
        }
        return iterator(t.first);
    }

    iterator insert(iterator pos, Record const & r) {
        PTR(RecordT) p = _table->copyRecord(r);
        key_type k = p->get(_key); 
        typename Internal::iterator i = _internal.insert(pos.base(), std::make_pair(k, p));
        if (i->second != p) {
            throw LSST_EXCEPT(
                lsst::pex::exceptions::InvalidParameterException,
                "Record with key '" + boost::lexical_cast<std::string>(k) + "' already present in Set."
            );
        }
        return iterator(i);
   }

    iterator insert(iterator pos, PTR(RecordT) const & p) {
        if (p->getTable() != _table) {
            throw LSST_EXCEPT(
                lsst::pex::exceptions::LogicErrorException,
                "Record to insert must be associated with the container's table."
            );
        }
        key_type k = p->get(_key);
        typename Internal::iterator i = _internal.insert(pos.base(), std::make_pair(k, p));
        if (i->second != p) {
            throw LSST_EXCEPT(
                lsst::pex::exceptions::InvalidParameterException,
                "Record with key '" + boost::lexical_cast<std::string>(k) + "' already present in Set."
            );
        }
        return iterator(i);
    }

    template <typename InputIterator>
    void insert(iterator pos, InputIterator first, InputIterator last, bool deep=false) {
        if (deep) {
            while (first != last) {
                pos = insert(pos, *first);
                ++first;
            }
        } else {
            while (first != last) {
                pos = insert(pos, first);
                ++first;
            }
        }
    }

    /**
     *  @brief Move the record pointed at by the given iterator to the correct place in the Set
     *         following a change to the record's key value.
     *
     *  This should be called any time the field corresponding to a record's unique ID is modified.
     *  If it is not called, the Set will remain sorted with respect to the original ID value.
     */
    void reinsert(iterator i) {
        PTR(RecordT) p = i;
        i = erase(i);
        insert(i, p);
    }

    void erase(iterator pos) { _internal.erase(pos.base()); }

    size_type erase(key_type const & k) { return _internal.erase(k); }

    void erase(iterator first, iterator last) {
        _internal.erase(first.base(), last.base());
    }

    void swap(SetT & other) {
        std::swap(_key, other._key);
        _table.swap(other._table);
        _internal.swap(other._internal);
    }

    void clear() { _internal.clear(); }

    key_compare key_comp() const { return _internal.key_comp(); }

    iterator find(key_type const & k) { return iterator(_internal.find(k)); }
    const_iterator find(key_type const & k) const { return const_iterator(_internal.find(k)); }

    size_type count(key_type const & k) const { return _internal.count(k); }

    iterator lower_bound(key_type const & k) { return iterator(_internal.lower_bound(k)); }
    const_iterator lower_bound(key_type const & k) const { return const_iterator(_internal.lower_bound(k)); }

    iterator upper_bound(key_type const & k) { return iterator(_internal.upper_bound(k)); }
    const_iterator upper_bound(key_type const & k) const { return const_iterator(_internal.upper_bound(k)); }

    std::pair<iterator,iterator> equal_range(key_type const & k) {
        std::pair<typename Internal::iterator, typename Internal::iterator> t = _internal.equal_range(k);
        return std::make_pair(iterator(t.first), iterator(t.second));
    }

    std::pair<const_iterator,const_iterator> equal_range(key_type const & k) const {
        std::pair<typename Internal::const_iterator, typename Internal::const_iterator> t
            = _internal.equal_range(k);
        return std::make_pair(const_iterator(t.first), const_iterator(t.second));
    }

private:

    Key<KeyT> _key;
    PTR(TableT) _table;
    Internal _internal;
};

template <typename RecordT, typename TableT>
template <typename OtherRecordT, typename OtherTableT, typename KeyT, typename CompareT>
VectorT<RecordT,TableT>::VectorT(SetT<OtherRecordT,OtherTableT,KeyT,CompareT> const & other) :
    _table(other.getTable()), _internal() 
{
    _internal.reserve(other.size());
    insert(end(), other.begin(), other.end(), false);
}

}}} // namespace lsst::afw::table

#endif // !AFW_TABLE_Set_h_INCLUDED
