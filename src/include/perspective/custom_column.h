/******************************************************************************
 *
 * Copyright (c) 2017, the Perspective Authors.
 *
 * This file is part of the Perspective library, distributed under the terms of
 * the Apache License 2.0.  The full license can be found in the LICENSE file.
 *
 */

#pragma once
#include <perspective/first.h>
#include <perspective/base.h>
#include <perspective/exports.h>

#include <vector>

namespace perspective
{

struct PERSPECTIVE_EXPORT t_custom_column_recipe
{
    t_custom_column_recipe() {}
    std::vector<t_str> m_icols;
    t_str m_ocol;
    t_str m_expr;
    std::vector<t_str> m_where_keys;
    std::vector<t_str> m_where_values;
    t_str m_base_case;
};

typedef std::vector<t_custom_column_recipe> t_custom_column_recipevec;

class PERSPECTIVE_EXPORT t_custom_column
{
public:
    t_custom_column(const t_custom_column_recipe& ccr);
    t_custom_column(const std::vector<t_str>& icols, const t_str& ocol,
        const t_str& expr, const std::vector<t_str>& where_keys,
        const std::vector<t_str>& where_values, const t_str& base_case);

    t_str get_ocol() const;
    t_str get_expr() const;
    const std::vector<t_str>& get_icols() const;
    t_custom_column_recipe get_recipe() const;
    const std::vector<t_str>& get_where_keys() const;
    const std::vector<t_str>& get_where_values() const;
    const t_str& get_base_case() const;

private:
    std::vector<t_str> m_icols;
    t_str m_ocol;
    t_str m_expr;
    std::vector<t_str> m_where_keys;
    std::vector<t_str> m_where_values;
    t_str m_base_case;
};

typedef std::vector<t_custom_column> t_ccol_vec;

} // end namespace perspective
