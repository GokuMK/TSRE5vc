/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef UIDOCUMENTFUNCTIONS_H
#define UIDOCUMENTFUNCTIONS_H

namespace Rml {
class DataModelConstructor;
}

namespace Ui {

// TSRE's functions for documents' data expressions (task scripting 02,
// step 10; the list: docs/tasks/scripting/rml-functions.md), registered on
// a document's data model (its context's: each display has its own):
// format_time, format_date, pad, default, add_minutes, minutes_until and
// cycle. Called as `{{ value | name(arguments) }}` or `name(arguments)`; an
// empty value gives empty text (default excepted), wrong arguments empty
// text and an RmlUi warning (the document's message for its author). They
// read nothing but their arguments: RmlUi evaluates an expression again
// only when a variable in it changed, so the clock is one of them where
// it counts (`clock | cycle(…)`, `minutes_until(clock)`).
void registerDocumentFunctions(Rml::DataModelConstructor &constructor);

}

#endif // UIDOCUMENTFUNCTIONS_H
