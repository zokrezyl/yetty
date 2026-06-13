#ifndef YETTY_YRICH_YRICH_H
#define YETTY_YRICH_YRICH_H

/*
 * yrich — document-centric WYSIWYG content for yetty.
 *
 * Umbrella header. Pulls in the building blocks plus the three concrete
 * document kinds (spreadsheet, slides, ydoc). Mirrors yetty-poc's yrich.h
 * but wired against the C ydraw buffer instead of YDrawBuffer.
 */

#include <yetty/yrich/yrich-types.h>
#include <yetty/yrich/yrich-selection.h>
#include <yetty/yrich/yrich-operation.h>
#include <yetty/yrich/yrich-command.h>
#include <yetty/yrich/element.h>
#include <yetty/yrich/document.h>
#include <yetty/yrich/spreadsheet.h>
#include <yetty/yrich/slides.h>
#include <yetty/yrich/ydoc.h>

#endif /* YETTY_YRICH_YRICH_H */
