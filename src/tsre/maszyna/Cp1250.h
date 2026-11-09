/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef CP1250_H
#define CP1250_H

#include <QString>

namespace Maszyna {

// MaSzyna's text is cp1250: names in .e3d, .mmd, .fiz and textures.txt. Its
// own table: not every Qt build has a codec for it.
QString fromCp1250(const char *text, int size);

}

#endif // CP1250_H
