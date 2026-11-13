/*
	Copyright 2006-2026 The QElectroTech Team
	This file is part of QElectroTech.

	QElectroTech is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 2 of the License, or
	(at your option) any later version.

	QElectroTech is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with QElectroTech.  If not, see <http://www.gnu.org/licenses/>.
*/
#ifndef TECSIDERACK_H
#define TECSIDERACK_H

#include <QList>
#include <QPointF>
#include <QSizeF>
#include <QString>

/**
	@brief The front view of a rack: which device goes where, the blind
	panels, shelves and frames around them. Pure geometry, in the units
	of the rack symbols of the company collection (build_rack.py): a 19"
	front is 220 px, 1U is 20 px, every symbol's origin its top-left
	corner and a frame's origin the top-left corner of its first unit.
*/
namespace TecsideRack
{
	const int unit = 20;		///< px of 1U
	const int rackWidth = 220;	///< px of a 19" front, ears included
	const int ear = 10;			///< px of a rack ear, where a shelf has none
	const int framePitch = 320;	///< px from a frame to the next one

	/**
		@brief A device of the location. @a units > 0 is mounted in the
		rack, @a size wide (220 px: full width, less: side by side with
		others in its units); @a units 0 is a desktop unit @a size big,
		put on a shelf. Without a front view (@a hasFront false) it is
		listed beside the rack.
	*/
	struct Device
	{
		QString name;
		int units = 0;
		QSizeF size;
		bool isAmp = false;
		bool hasFront = true;
	};

	struct Placement
	{
		enum Kind {Frame, Blank, Shelf, Device};
		Kind kind = Device;
		int device = -1;	///< index in the devices given, for a Device
		int units = 0;		///< height of a Frame, Blank or Shelf
		int frame = 0;		///< the frame it stands in
		QPointF pos;		///< top-left corner, frame 0's first unit at (0, 0)
	};

	struct Layout
	{
		QList<Placement> placements;
		QList<int> frames;		///< the units of each frame, left to right
		QList<int> notInRack;	///< devices without a front, in order
		QList<int> tooWide;		///< desktop units wider than a shelf
		int units = 0;			///< units taken, blanks included

		QList<Placement> of(Placement::Kind kind) const;
		const Placement *placementOf(int device) const;
	};

	Layout layout(const QList<Device> &devices, QList<int> frame_sizes,
				  QList<int> shelf_sizes = {1, 2});
}

#endif
