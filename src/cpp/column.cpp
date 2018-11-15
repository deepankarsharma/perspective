/******************************************************************************
 *
 * Copyright (c) 2017, the Perspective Authors.
 *
 * This file is part of the Perspective library, distributed under the terms of
 * the Apache License 2.0.  The full license can be found in the LICENSE file.
 *
 */

#include <perspective/first.h>
#include <perspective/portable.h>
SUPPRESS_WARNINGS_VC(4505)
#include <perspective/column.h>
#include <perspective/defaults.h>
#include <perspective/base.h>
#include <perspective/sym_table.h>
#include <perspective/vocab.h>
#include <perspective/mask.h>
#include <unordered_set>

namespace perspective
{
// TODO : move to delegated constructors in C++11

t_column_recipe::t_column_recipe()
    : m_vlenidx(0)
    , m_size(0)
{
}

t_column::t_column()
    : m_dtype(DTYPE_NONE)
    , m_init(false)
    , m_isvlen(false)
    , m_data(nullptr)
    , m_vocab(nullptr)
    , m_status(nullptr)
    , m_size(0)
    , m_status_enabled(false)
    , m_from_recipe(false)

{
    LOG_CONSTRUCTOR("t_column");
}

t_column::t_column(const t_column_recipe& recipe)
    : m_dtype(recipe.m_dtype)
    , m_init(false)
    , m_size(recipe.m_size)
    , m_status_enabled(recipe.m_status_enabled)
    , m_from_recipe(true)

{
    LOG_CONSTRUCTOR("t_column");
    m_data.reset(new t_lstore(recipe.m_data));
    m_isvlen = is_vlen_dtype(recipe.m_dtype);

    if (m_isvlen)
    {
        m_vocab.reset(new t_vocab(recipe));
    }
    else
    {
        m_vocab.reset(new t_vocab);
    }

    if (m_status_enabled)
    {
        m_status.reset(new t_lstore(recipe.m_status));
    }
    else
    {
        m_status.reset(new t_lstore);
    }
}

void
t_column::column_copy_helper(const t_column& other)
{
    m_dtype = other.m_dtype;
    m_init = false;
    m_isvlen = other.m_isvlen;
    m_data.reset(new t_lstore(other.m_data->get_recipe()));
    m_vocab.reset(new t_vocab(other.m_vocab->get_vlendata()->get_recipe(),
        other.m_vocab->get_extents()->get_recipe()));
    m_status.reset(new t_lstore(other.m_status->get_recipe()));

    m_size = other.m_size;
    m_status_enabled = other.m_status_enabled;
    m_from_recipe = false;
}

t_column::t_column(const t_column& c)
{
    PSP_VERBOSE_ASSERT(this != &c, "Assigning self");
    column_copy_helper(c);
    m_init = false;
}

t_column&
t_column::operator=(const t_column& c)
{
    PSP_VERBOSE_ASSERT(this != &c, "Assigning self");
    column_copy_helper(c);
    m_init = false;
    return *this;
}

t_column::t_column(
    t_dtype dtype, t_bool missing_enabled, const t_lstore_recipe& a)
    : t_column(dtype, missing_enabled, a, a.m_capacity / get_dtype_size(dtype))
{
}

t_column::t_column(t_dtype dtype, t_bool missing_enabled, t_uindex row_capacity)
    : t_column(dtype, missing_enabled,
          t_lstore_recipe(row_capacity * get_dtype_size(dtype)), row_capacity)
{
}

t_column::t_column(t_dtype dtype, t_bool missing_enabled,
    const t_lstore_recipe& a, t_uindex row_capacity)
    : m_dtype(dtype)
    , m_init(false)
    , m_size(0)
    , m_status_enabled(missing_enabled)
    , m_from_recipe(false)
{

    m_data.reset(new t_lstore(a));
    // TODO make sure that capacity from a
    // is not causing an overrreserve in places
    // most notably in valid columns
    LOG_CONSTRUCTOR("t_column");
    m_isvlen = is_vlen_dtype(m_dtype);

    if (is_vlen_dtype(dtype))
    {
        t_lstore_recipe vlendata_args(a);
        t_lstore_recipe extents_args(a);

        vlendata_args.m_capacity = DEFAULT_EMPTY_CAPACITY;
        extents_args.m_capacity = DEFAULT_EMPTY_CAPACITY;

        vlendata_args.m_colname = a.m_colname + t_str("_vlendata");
        extents_args.m_colname = a.m_colname + t_str("_extents");

        m_vocab.reset(new t_vocab(vlendata_args, extents_args));
    }
    else
    {
        m_vocab.reset(new t_vocab);
    }

    if (is_status_enabled())
    {
        t_lstore_recipe missing_args(a);
        missing_args.m_capacity = row_capacity;

        missing_args.m_colname = a.m_colname + t_str("_missing");
        m_status.reset(new t_lstore(missing_args));
    }
    else
    {
        m_status.reset(new t_lstore);
    }
}

t_col_sptr
t_column::build(t_dtype dtype, const t_tscalvec& vec)
{
    auto rv = std::make_shared<t_column>(dtype, true, vec.size());
    rv->init();
    for (const auto& s : vec)
    {
        rv->push_back(s);
    }
    return rv;
}

bool
t_column::is_status_enabled() const
{
    return m_status_enabled;
}

void
t_column::init()
{
    LOG_INIT("t_column");

    m_data->init();

    if (is_vlen_dtype(m_dtype))
    {
        m_vocab->init(m_from_recipe);
    }

    if (is_status_enabled())
    {
        m_status->init();
    }

    if (is_deterministic_sized(m_dtype))
        m_elemsize = get_dtype_size(m_dtype);
    m_init = true;
    COLUMN_CHECK_VALUES();
}

t_column::~t_column() { LOG_DESTRUCTOR("t_column"); }

t_dtype
t_column::get_dtype() const
{
    return m_dtype;
}

// extend based on dtype size
void
t_column::extend_dtype(t_uindex idx)
{
    t_uindex new_extents = idx * get_dtype_size(m_dtype);
    m_data->reserve(new_extents);
    m_data->set_size(new_extents);
    m_size = m_data->size() / get_dtype_size(m_dtype);

    if (is_status_enabled())
    {
        t_uindex sz = idx * get_dtype_size(DTYPE_UINT8);
        m_status->reserve(sz);
        m_status->set_size(sz);
    }
}

t_uindex
t_column::get_interned(const t_str& s)
{
    COLUMN_CHECK_STRCOL();
    return m_vocab->get_interned(s);
}
t_uindex
t_column::get_interned(const char* s)
{
    COLUMN_CHECK_STRCOL();
    return m_vocab->get_interned(s);
}

template <>
void
t_column::push_back<const char*>(const char* elem)
{
    COLUMN_CHECK_STRCOL();
    if (!elem)
    {
        m_data->push_back(static_cast<t_uindex>(0));
        return;
    }

    t_uindex idx = m_vocab->get_interned(elem);
    m_data->push_back(idx);
    ++m_size;
}

template <>
void
t_column::push_back<char*>(char* elem)
{
    COLUMN_CHECK_STRCOL();
    t_uindex idx = m_vocab->get_interned(elem);
    m_data->push_back(idx);
    ++m_size;
}

template <>
void
t_column::push_back<std::string>(std::string elem)
{
    COLUMN_CHECK_STRCOL();
    push_back(elem.c_str());
    ++m_size;
}

template <>
void
t_column::push_back<const char*>(const char* elem, t_status status)
{
    COLUMN_CHECK_STRCOL();
    push_back(elem);
    m_status->push_back(status);
    ++m_size;
}

template <>
void
t_column::push_back<char*>(char* elem, t_status status)
{
    COLUMN_CHECK_STRCOL();
    push_back(elem);
    m_status->push_back(status);
    ++m_size;
}

template <>
void
t_column::push_back<std::string>(std::string elem, t_status status)
{
    COLUMN_CHECK_STRCOL();
    push_back(elem);
    m_status->push_back(status);
    ++m_size;
}

template <>
PERSPECTIVE_EXPORT void
t_column::push_back<t_tscalar>(t_tscalar elem)
{
    elem.m_type = m_dtype;

    switch (m_dtype)
    {
        case DTYPE_NONE:
        {
            PSP_COMPLAIN_AND_ABORT("Encountered none");
        }
        break;
        case DTYPE_INT64:
        {
            push_back(elem.get<t_int64>(), elem.m_status);
        }
        break;
        case DTYPE_INT32:
        {
            push_back(elem.get<t_int32>(), elem.m_status);
        }
        break;
        case DTYPE_INT16:
        {
            push_back(elem.get<t_int16>(), elem.m_status);
        }
        break;
        case DTYPE_INT8:
        {
            push_back(elem.get<t_int8>(), elem.m_status);
        }
        break;
        case DTYPE_UINT64:
        {
            push_back(elem.get<t_uint64>(), elem.m_status);
        }
        break;
        case DTYPE_UINT32:
        {
            push_back(elem.get<t_uint32>(), elem.m_status);
        }
        break;
        case DTYPE_UINT16:
        {
            push_back(elem.get<t_uint16>(), elem.m_status);
        }
        break;
        case DTYPE_UINT8:
        {
            push_back(elem.get<t_uint8>(), elem.m_status);
        }
        break;
        case DTYPE_FLOAT64:
        {
            push_back(elem.get<t_float64>(), elem.m_status);
        }
        break;
        case DTYPE_FLOAT32:
        {
            push_back(elem.get<t_float32>(), elem.m_status);
        }
        break;
        case DTYPE_BOOL:
        {
            push_back(elem.get<t_bool>(), elem.m_status);
        }
        break;
        case DTYPE_TIME:
        {
            push_back(elem.get<t_int64>(), elem.m_status);
        }
        break;
        case DTYPE_DATE:
        {
            push_back(elem.get<t_uint32>(), elem.m_status);
        }
        break;
        case DTYPE_STR:
        {
            push_back(elem.get<const char*>(), elem.m_status);
        }
        break;
        default:
        {
            PSP_COMPLAIN_AND_ABORT("Unexpected type");
        }
    }
    ++m_size;
}

const t_lstore&
t_column::data_lstore() const
{
    return *m_data;
}

t_uindex
t_column::size() const
{
    return m_size;
}

void
t_column::set_size(t_uindex size)
{
#ifdef PSP_COLUMN_VERIFY
    PSP_VERBOSE_ASSERT(size * get_dtype_size(m_dtype) <= m_data->capacity(),
        "Not enough space reserved for column");
#endif
    m_size = size;
    m_data->set_size(m_elemsize * size);

    if (is_status_enabled())
        m_status->set_size(get_dtype_size(DTYPE_UINT8) * size);
}

void
t_column::reserve(t_uindex size)
{
    m_data->reserve(get_dtype_size(m_dtype) * size);
    if (is_status_enabled())
        m_status->reserve(get_dtype_size(DTYPE_UINT8) * size);
}

t_lstore*
t_column::_get_data_lstore()
{
    return m_data.get();
}

t_vocab*
t_column::_get_vocab()
{
    return m_vocab.get();
}

t_uindex
t_column::get_vlenidx() const
{
    return m_vocab->get_vlenidx();
}

t_tscalar
t_column::get_scalar(t_uindex idx) const
{
    COLUMN_CHECK_ACCESS(idx);
    t_tscalar rv;
    rv.clear();

    switch (m_dtype)
    {
        case DTYPE_NONE:
        {
        }
        break;
        case DTYPE_INT64:
        {
            rv.set(*(m_data->get_nth<t_int64>(idx)));
        }
        break;
        case DTYPE_INT32:
        {
            rv.set(*(m_data->get_nth<t_int32>(idx)));
        }
        break;
        case DTYPE_INT16:
        {
            rv.set(*(m_data->get_nth<t_int16>(idx)));
        }
        break;
        case DTYPE_INT8:
        {
            rv.set(*(m_data->get_nth<t_int8>(idx)));
        }
        break;

        case DTYPE_UINT64:
        {
            rv.set(*(m_data->get_nth<t_uint64>(idx)));
        }
        break;
        case DTYPE_UINT32:
        {
            rv.set(*(m_data->get_nth<t_uint32>(idx)));
        }
        break;
        case DTYPE_UINT16:
        {
            rv.set(*(m_data->get_nth<t_uint16>(idx)));
        }
        break;
        case DTYPE_UINT8:
        {
            rv.set(*(m_data->get_nth<t_uint8>(idx)));
        }
        break;

        case DTYPE_FLOAT64:
        {
            rv.set(*(m_data->get_nth<t_float64>(idx)));
        }
        break;
        case DTYPE_FLOAT32:
        {
            rv.set(*(m_data->get_nth<t_float32>(idx)));
        }
        break;
        case DTYPE_BOOL:
        {
            rv.set(*(m_data->get_nth<t_bool>(idx)));
        }
        break;
        case DTYPE_TIME:
        {
            const t_time::t_rawtype* v
                = m_data->get_nth<t_time::t_rawtype>(idx);
            rv.set(t_time(*v));
        }
        break;
        case DTYPE_DATE:
        {
            const t_date::t_rawtype* v
                = m_data->get_nth<t_date::t_rawtype>(idx);
            rv.set(t_date(*v));
        }
        break;
        case DTYPE_STR:
        {
            COLUMN_CHECK_STRCOL();
            const t_uindex* sidx = m_data->get_nth<t_uindex>(idx);
            rv.set(m_vocab->unintern_c(*sidx));
        }
        break;
        case DTYPE_F64PAIR:
        {
            const t_f64pair* pair = m_data->get_nth<t_f64pair>(idx);
            rv.set(pair->first / pair->second);
        }
        break;
        default:
        {
            PSP_COMPLAIN_AND_ABORT("Unexpected type");
        }
    }

    if (is_status_enabled())
        rv.m_status = *get_nth_status(idx);
    return rv;
}

void
t_column::unset(t_uindex idx)
{
    clear(idx, STATUS_CLEAR);
}

void
t_column::clear(t_uindex idx)
{
    clear(idx, STATUS_INVALID);
}

void
t_column::clear(t_uindex idx, t_status status)
{
    switch (m_dtype)
    {
        case DTYPE_STR:
        {
            t_stridx v = 0;
            set_nth<t_stridx>(idx, v, status);
        }
        break;
        case DTYPE_TIME:
        case DTYPE_FLOAT64:
        case DTYPE_UINT64:
        case DTYPE_INT64:
        {
            t_uint64 v = 0;
            set_nth<t_uint64>(idx, v, status);
        }
        break;
        case DTYPE_DATE:
        case DTYPE_FLOAT32:
        case DTYPE_UINT32:
        case DTYPE_INT32:
        {
            t_uint32 v = 0;
            set_nth<t_uint32>(idx, v, status);
        }
        break;
        case DTYPE_UINT16:
        case DTYPE_INT16:
        {
            t_uint16 v = 0;
            set_nth<t_uint16>(idx, v, status);
        }
        break;
        case DTYPE_BOOL:
        case DTYPE_UINT8:
        case DTYPE_INT8:
        {
            t_uint8 v = 0;
            set_nth<t_uint8>(idx, v, status);
        }
        break;
        case DTYPE_F64PAIR:
        {
            std::pair<t_uint64, t_uint64> v;
            v.first = 0;
            v.second = 0;
            set_nth<std::pair<t_uint64, t_uint64>>(idx, v, status);
        }
        break;
        default:
        {
            PSP_COMPLAIN_AND_ABORT("Unexpected type");
        }
    }
}

template <>
char*
t_column::get_nth<char>(t_uindex idx)
{
    PSP_COMPLAIN_AND_ABORT("Unsafe operation detected");
    ++idx;
    return 0;
}

template <>
const char*
t_column::get_nth<const char>(t_uindex idx) const
{
    COLUMN_CHECK_ACCESS(idx);
    COLUMN_CHECK_STRCOL();
    const t_uindex* sidx = get_nth<t_uindex>(idx);
    return m_vocab->unintern_c(*sidx);
}

// idx is in items
const t_status*
t_column::get_nth_status(t_uindex idx) const
{
    PSP_VERBOSE_ASSERT(is_status_enabled(), "Status not available for column");
    COLUMN_CHECK_ACCESS(idx);
    t_status* status = m_status->get_nth<t_status>(idx);
    return status;
}

t_bool
t_column::is_valid(t_uindex idx) const
{
    PSP_VERBOSE_ASSERT(is_status_enabled(), "Status not available for column");
    COLUMN_CHECK_ACCESS(idx);
    t_status status = *m_status->get_nth<t_status>(idx);
    return status == STATUS_VALID;
}

t_bool
t_column::is_cleared(t_uindex idx) const
{
    PSP_VERBOSE_ASSERT(is_status_enabled(), "Status not available for column");
    COLUMN_CHECK_ACCESS(idx);
    t_status status = *m_status->get_nth<t_status>(idx);
    return status == STATUS_CLEAR;
}

template <>
void
t_column::set_nth<const char*>(t_uindex idx, const char* elem)
{
    COLUMN_CHECK_STRCOL();
    set_nth_body(idx, elem, STATUS_VALID);
}

template <>
void
t_column::set_nth<t_str>(t_uindex idx, t_str elem)
{
    COLUMN_CHECK_STRCOL();
    set_nth(idx, elem.c_str(), STATUS_VALID);
}

template <>
void
t_column::set_nth<const char*>(t_uindex idx, const char* elem, t_status status)
{
    COLUMN_CHECK_STRCOL();
    set_nth_body(idx, elem, status);
}

template <>
void
t_column::set_nth<t_str>(t_uindex idx, t_str elem, t_status status)
{
    COLUMN_CHECK_STRCOL();
    set_nth(idx, elem.c_str(), status);
}

void
t_column::set_valid(t_uindex idx, t_bool valid)
{
    set_status(idx, valid ? STATUS_VALID : STATUS_INVALID);
}

void
t_column::set_status(t_uindex idx, t_status status)
{
    m_status->set_nth<t_status>(idx, status);
}

void
t_column::set_scalar(t_uindex idx, t_tscalar value)
{
    COLUMN_CHECK_ACCESS(idx);
    value.m_type = m_dtype;

    switch (m_dtype)
    {
        case DTYPE_NONE:
        {
        }
        break;
        case DTYPE_INT64:
        {
            t_int64 tgt = value.get<t_int64>();
            set_nth<t_int64>(idx, tgt, value.m_status);
        }
        break;
        case DTYPE_INT32:
        {
            t_int32 tgt = value.get<t_int32>();
            set_nth<t_int32>(idx, tgt, value.m_status);
        }
        break;
        case DTYPE_INT16:
        {
            t_int16 tgt = value.get<t_int16>();
            set_nth<t_int16>(idx, tgt, value.m_status);
        }
        break;
        case DTYPE_INT8:
        {
            t_int8 tgt = value.get<t_int8>();
            set_nth<t_int8>(idx, tgt, value.m_status);
        }
        break;
        case DTYPE_UINT64:
        {
            t_uint64 tgt = value.get<t_uint64>();
            set_nth<t_uint64>(idx, tgt, value.m_status);
        }
        break;
        case DTYPE_UINT32:
        {
            t_uint32 tgt = value.get<t_uint32>();
            set_nth<t_uint32>(idx, tgt, value.m_status);
        }
        break;
        case DTYPE_UINT16:
        {
            t_uint16 tgt = value.get<t_uint16>();
            set_nth<t_uint16>(idx, tgt, value.m_status);
        }
        break;
        case DTYPE_UINT8:
        {
            t_uint8 tgt = value.get<t_uint8>();
            set_nth<t_uint8>(idx, tgt, value.m_status);
        }
        break;
        case DTYPE_FLOAT64:
        {
            t_float64 tgt = value.get<t_float64>();
            set_nth<t_float64>(idx, tgt, value.m_status);
        }
        break;
        case DTYPE_FLOAT32:
        {
            t_float32 tgt = value.get<t_float32>();
            set_nth<t_float32>(idx, tgt, value.m_status);
        }
        break;
        case DTYPE_BOOL:
        {
            t_bool tgt = value.get<t_bool>();
            set_nth<t_bool>(idx, tgt, value.m_status);
        }
        break;
        case DTYPE_TIME:
        {
            t_time tgt = value.get<t_time>();
            set_nth<t_time>(idx, tgt, value.m_status);
        }
        break;
        case DTYPE_DATE:
        {
            t_date tgt = value.get<t_date>();
            set_nth<t_date>(idx, tgt, value.m_status);
        }
        break;
        case DTYPE_STR:
        {
            COLUMN_CHECK_STRCOL();
            const char* tgt = value.get_char_ptr();
            t_str empty;

            if (tgt)
            {
                PSP_VERBOSE_ASSERT(value.m_type == DTYPE_STR,
                    "Setting non string scalar on string column");
                set_nth<const char*>(idx, tgt, value.m_status);
            }
            else
            {
                set_nth<const char*>(idx, empty.c_str(), value.m_status);
            }
        }
        break;
        default:
        {
            PSP_COMPLAIN_AND_ABORT("Unexpected type");
        }
    }
}

t_bool
t_column::is_vlen() const
{
    return is_vlen_dtype(m_dtype);
}

void
t_column::append(const t_column& other)
{
    PSP_VERBOSE_ASSERT(m_dtype == other.m_dtype, "Mismatched dtypes detected");
    if (is_vlen())
    {
        if (size() == 0)
        {

            m_data->fill(*other.m_data);

            if (other.is_status_enabled())
            {
                m_status->fill(*other.m_status);
            }

            m_vocab->fill(*(other.m_vocab->get_vlendata()),
                *(other.m_vocab->get_extents()), other.m_vocab->get_vlenidx());

            set_size(other.size());
            m_vocab->rebuild_map();
        }
        else
        {
            for (t_uindex idx = 0, loop_end = other.size(); idx < loop_end;
                 ++idx)
            {
                const char* s = other.get_nth<const char>(idx);
                push_back(s);
            }

            if (is_status_enabled())
            {
                m_status->append(*other.m_status);
            }
        }
    }
    else
    {
        m_data->append(*other.m_data);

        if (is_status_enabled())
        {
            m_status->append(*other.m_status);
        }
    }

    COLUMN_CHECK_VALUES();
}

void
t_column::clear()
{
    m_data->set_size(0);
    if (m_dtype == DTYPE_STR)
        m_data->clear();
    if (is_status_enabled())
    {
        m_status->clear();
    }
    m_size = 0;
}

void
t_column::pprint() const
{
    for (t_uindex idx = 0, loop_end = size(); idx < loop_end; ++idx)
    {
        std::cout << idx << " => " << get_scalar(idx) << std::endl;
    }
}

t_column_recipe
t_column::get_recipe() const
{
    t_column_recipe rval;
    rval.m_dtype = m_dtype;
    rval.m_data = m_data->get_recipe();
    rval.m_isvlen = is_vlen_dtype(m_dtype);

    if (rval.m_isvlen)
    {
        rval.m_vlendata = m_vocab->get_vlendata()->get_recipe();
        rval.m_extents = m_vocab->get_extents()->get_recipe();
    }

    rval.m_status_enabled = m_status_enabled;
    if (m_status_enabled)
    {
        rval.m_status = m_status->get_recipe();
    }

    rval.m_vlenidx = get_vlenidx();
    rval.m_size = m_size;
    return rval;
}

void
t_column::copy_vocabulary(const t_column* other)
{
#ifdef PSP_COLUMN_VERIFY
    other->verify();
#endif
    COLUMN_CHECK_STRCOL();
    m_vocab->copy_vocabulary(*(other->m_vocab.get()));
    COLUMN_CHECK_VALUES();
}

void
t_column::pprint_vocabulary() const
{
    if (!is_vlen_dtype(m_dtype))
        return;
    m_vocab->pprint_vocabulary();
}

t_col_sptr
t_column::clone() const
{
    auto rval = std::make_shared<t_column>(*this);
    rval->init();
    rval->set_size(size());
    rval->m_data->fill(*m_data);

    if (rval->is_status_enabled())
    {
        rval->m_status->fill(*m_status);
    }

    if (is_vlen_dtype(get_dtype()))
    {
        rval->m_vocab->clone(*m_vocab);
    }

#ifdef PSP_COLUMN_VERIFY
    rval->verify();
#endif
    return rval;
}

t_col_sptr
t_column::clone(const t_mask& mask) const
{
    if (mask.count() == size())
    {
        return clone();
    }

    auto rval = std::make_shared<t_column>(*this);
    rval->init();
    rval->set_size(mask.size());

    rval->m_data->fill(*m_data, mask, get_dtype_size(get_dtype()));

    if (rval->is_status_enabled())
    {
        rval->m_status->fill(*m_status, mask, sizeof(t_status));
    }

    if (is_vlen_dtype(get_dtype()))
    {
        rval->m_vocab->clone(*m_vocab);
    }
#ifdef PSP_COLUMN_VERIFY
    rval->verify();
#endif
    return rval;
}

void
t_column::valid_raw_fill()
{
    m_status->raw_fill(STATUS_VALID);
}

void
t_column::copy(const t_column* other, const std::vector<t_uindex>& indices,
    t_uindex offset)
{
    PSP_VERBOSE_ASSERT(
        m_dtype == other->get_dtype(), "Cannot copy from diff dtype");

    switch (m_dtype)
    {
        case DTYPE_NONE:
        {
            return;
        }
        case DTYPE_INT64:
        {
            copy_helper<t_int64>(other, indices, offset);
        }
        break;
        case DTYPE_INT32:
        {
            copy_helper<t_int32>(other, indices, offset);
        }
        break;
        case DTYPE_INT16:
        {
            copy_helper<t_int16>(other, indices, offset);
        }
        break;
        case DTYPE_INT8:
        {
            copy_helper<t_int8>(other, indices, offset);
        }
        break;
        case DTYPE_UINT64:
        {
            copy_helper<t_uint64>(other, indices, offset);
        }
        break;
        case DTYPE_UINT32:
        {
            copy_helper<t_uint32>(other, indices, offset);
        }
        break;
        case DTYPE_UINT16:
        {
            copy_helper<t_uint16>(other, indices, offset);
        }
        break;
        case DTYPE_UINT8:
        {
            copy_helper<t_uint8>(other, indices, offset);
        }
        break;
        case DTYPE_FLOAT64:
        {
            copy_helper<t_float64>(other, indices, offset);
        }
        break;
        case DTYPE_FLOAT32:
        {
            copy_helper<t_float32>(other, indices, offset);
        }
        break;
        case DTYPE_BOOL:
        {
            copy_helper<t_uint8>(other, indices, offset);
        }
        break;
        case DTYPE_TIME:
        {
            copy_helper<t_int64>(other, indices, offset);
        }
        break;
        case DTYPE_DATE:
        {
            copy_helper<t_uint32>(other, indices, offset);
        }
        break;
        case DTYPE_STR:
        {
            copy_helper<const char>(other, indices, offset);
        }
        break;
        default:
        {
            PSP_COMPLAIN_AND_ABORT("Unexpected type");
        }
    }
}

template <>
void
t_column::copy_helper<const char>(const t_column* other,
    const std::vector<t_uindex>& indices, t_uindex offset)
{
    t_uindex eidx
        = std::min(other->size(), static_cast<t_uindex>(indices.size()));
    reserve(eidx + offset);

    for (t_uindex idx = 0; idx < eidx; ++idx)
    {
        set_scalar(offset + idx, other->get_scalar(indices[idx]));
    }
    COLUMN_CHECK_VALUES();
}

template <>
void
t_column::fill(std::vector<const char*>& vec, const t_uindex* bidx,
    const t_uindex* eidx) const
{

    PSP_VERBOSE_ASSERT(eidx - bidx > 0, "Invalid pointers passed in");

    for (t_uindex idx = 0, loop_end = eidx - bidx; idx < loop_end; ++idx)

    {
        vec[idx] = get_nth<const char>(*(bidx + idx));
    }
    COLUMN_CHECK_VALUES();
}

void
t_column::verify() const
{
    verify_size();
}

void
t_column::verify_size(t_uindex idx) const
{
    if (m_dtype == DTYPE_USER_FIXED)
        return;

    PSP_VERBOSE_ASSERT(idx * get_dtype_size(m_dtype) <= m_data->capacity(),
        "Not enough space reserved for column");

    PSP_VERBOSE_ASSERT(idx * get_dtype_size(m_dtype) <= m_data->capacity(),
        "Not enough space reserved for column");

    if (is_status_enabled())
    {
        PSP_VERBOSE_ASSERT(
            idx * get_dtype_size(DTYPE_UINT8) <= m_status->capacity(),
            "Not enough space reserved for column");
    }

    if (is_vlen_dtype(m_dtype))
    {
        m_vocab->verify_size();
    }
}

void
t_column::verify_size() const
{
    verify_size(size());
}

const char*
t_column::unintern_c(t_uindex idx) const
{
    return m_vocab->unintern_c(idx);
}

void
t_column::_rebuild_map()
{
    m_vocab->rebuild_map();
}

void
t_column::borrow_vocabulary(const t_column& o)
{
    m_vocab = const_cast<t_column&>(o).m_vocab;
}

void
t_column::set_vocabulary(const std::vector<std::pair<t_tscalar, t_uindex>>& vocab, size_t total_size)
{
    if (total_size)
        m_vocab->reserve(total_size, vocab.size() + 1);

    for (const auto& kv : vocab)
        m_vocab->get_interned(kv.first.get_char_ptr());
}

void
t_column::set_nth_body(t_uindex idx, const char* elem, t_status status)
{
    COLUMN_CHECK_ACCESS(idx);
    PSP_VERBOSE_ASSERT(m_dtype == DTYPE_STR, "Setting non string column");
    t_uindex interned = m_vocab->get_interned(elem);
    m_data->set_nth<t_uindex>(idx, interned);

    if (is_status_enabled())
    {
        m_status->set_nth<t_status>(idx, status);
    }
}
} // end namespace perspective
