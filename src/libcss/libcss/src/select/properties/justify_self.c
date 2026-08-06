/*
 * This file is part of LibCSS
 * Licensed under the MIT License,
 *		  http://www.opensource.org/licenses/mit-license.php
 *
 * justify-self (css-align-3) — grid inline-axis self-alignment. Added
 * in-tree; keyword-only (the <overflow-position> and legacy modifiers are
 * not modelled).
 */

#include "bytecode/bytecode.h"
#include "bytecode/opcodes.h"
#include "select/propset.h"
#include "select/propget.h"
#include "utils/utils.h"

#include "select/properties/properties.h"
#include "select/properties/helpers.h"

css_error css__cascade_justify_self(uint32_t opv, css_style *style, css_select_state *state)
{
    uint16_t value = CSS_JUSTIFY_SELF_INHERIT;

    UNUSED(style);

    if (hasFlagValue(opv) == false) {
        switch (getValue(opv)) {
        case JUSTIFY_SELF_STRETCH:
            value = CSS_JUSTIFY_SELF_STRETCH;
            break;
        case JUSTIFY_SELF_FLEX_START:
            value = CSS_JUSTIFY_SELF_FLEX_START;
            break;
        case JUSTIFY_SELF_FLEX_END:
            value = CSS_JUSTIFY_SELF_FLEX_END;
            break;
        case JUSTIFY_SELF_CENTER:
            value = CSS_JUSTIFY_SELF_CENTER;
            break;
        case JUSTIFY_SELF_BASELINE:
            value = CSS_JUSTIFY_SELF_BASELINE;
            break;
        case JUSTIFY_SELF_AUTO:
            value = CSS_JUSTIFY_SELF_AUTO;
            break;
        case JUSTIFY_SELF_START:
            value = CSS_JUSTIFY_SELF_START;
            break;
        case JUSTIFY_SELF_END:
            value = CSS_JUSTIFY_SELF_END;
            break;
        case JUSTIFY_SELF_LEFT:
            value = CSS_JUSTIFY_SELF_LEFT;
            break;
        case JUSTIFY_SELF_RIGHT:
            value = CSS_JUSTIFY_SELF_RIGHT;
            break;
        }
    }

    if (css__outranks_existing(getOpcode(opv), isImportant(opv), state, getFlagValue(opv))) {
        return set_justify_self(state->computed, value);
    }

    return CSS_OK;
}

css_error css__set_justify_self_from_hint(const css_hint *hint, css_computed_style *style)
{
    return set_justify_self(style, hint->status);
}

css_error css__initial_justify_self(css_select_state *state)
{
    return set_justify_self(state->computed, CSS_JUSTIFY_SELF_AUTO);
}

css_error css__copy_justify_self(const css_computed_style *from, css_computed_style *to)
{
    if (from == to) {
        return CSS_OK;
    }

    return set_justify_self(to, get_justify_self(from));
}

css_error css__compose_justify_self(const css_computed_style *parent,
                                    const css_computed_style *child, css_computed_style *result)
{
    uint8_t type = get_justify_self(child);

    return css__copy_justify_self(type == CSS_JUSTIFY_SELF_INHERIT ? parent : child, result);
}
