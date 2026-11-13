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
#include "tecside/tecsiderack.h"

#include <algorithm>
#include <cmath>

namespace
{
	using namespace TecsideRack;

		//What takes some units of the rack, top to bottom
	struct Slot
	{
		enum Kind {Single, Row, Shelf, Blank};
		Kind kind = Single;
		int units = 0;
		QList<int> items;	///< the devices in it
		qreal used = 0;		///< width taken in a Row or on a Shelf
		bool amp = false;
	};

	const qreal shelf_width = rackWidth - 2 * ear;

	bool isFullWidth(const TecsideRack::Device &device)
	{
		return device.size.width() <= 0 || device.size.width() > rackWidth - 0.5;
	}

		//The smallest of @p sizes that holds @p units, else the largest
	int smallestHolding(QList<int> sizes, int units)
	{
		std::sort(sizes.begin(), sizes.end());
		for (int size : sizes)
			if (size >= units)
				return size;
		return sizes.isEmpty() ? units : sizes.last();
	}

		//Side by side in the open Row or Shelf of @p list at @p open, a new
		//one when it is full
	void addSideBySide(QList<Slot> &list, int &open, Slot::Kind kind,
					   qreal width, int index, qreal item_width, int item_units)
	{
		if (open < 0 || list.at(open).used + item_width > width) {
			Slot slot;
			slot.kind = kind;
			list << slot;
			open = list.size() - 1;
		}
		Slot &slot = list[open];
		slot.items << index;
		slot.used += item_width;
		slot.units = std::max({1, slot.units, item_units});
	}
}

QList<TecsideRack::Placement> TecsideRack::Layout::of(Placement::Kind kind) const
{
	QList<Placement> list;
	for (const Placement &placement : placements)
		if (placement.kind == kind)
			list << placement;
	return list;
}

const TecsideRack::Placement *TecsideRack::Layout::placementOf(int device) const
{
	for (const Placement &placement : placements)
		if (placement.kind == Placement::Device && placement.device == device)
			return &placement;
	return nullptr;
}

/**
	@return where @p devices go in racks of the @p frame_sizes units on
	hand, with shelves of @p shelf_sizes units:
	- top to bottom in the order given, a full width device in its units,
	  narrower rack devices side by side in a row of units, desktop units
	  side by side on a shelf (a new row or shelf when the 19" are full);
	  a row or shelf stands where its first device comes;
	- the rack mounted amplifiers last, at the bottom of the frame, one
	  above the other with a 1U blind panel between two: two amplifiers
	  never touch;
	- the smallest frame that holds it all; past the largest frame, as
	  many of those as needed side by side, then the smallest that holds
	  the rest. A blind panel that would stand at the top or bottom of a
	  frame is left out;
	- the devices without a front listed in notInRack.
*/
TecsideRack::Layout TecsideRack::layout(const QList<Device> &devices,
										QList<int> frame_sizes,
										QList<int> shelf_sizes)
{
	Layout result;
	if (frame_sizes.isEmpty())
		frame_sizes << 42;
	const int largest = *std::max_element(frame_sizes.cbegin(), frame_sizes.cend());

	QList<Slot> top, amps;
	int row = -1, shelf = -1;
	for (int i = 0 ; i < devices.size() ; ++i) {
		const Device &device = devices.at(i);
		if (!device.hasFront) {
			result.notInRack << i;
			continue;
		}
		if (device.units > 0 && device.isAmp) {
			if (!amps.isEmpty()) {
				Slot blank;
				blank.kind = Slot::Blank;
				blank.units = 1;
				amps << blank;
			}
			Slot slot;
			slot.units = device.units;
			slot.items << i;
			slot.amp = true;
			amps << slot;
		} else if (device.units > 0 && isFullWidth(device)) {
			Slot slot;
			slot.units = device.units;
			slot.items << i;
			top << slot;
		} else if (device.units > 0) {
			addSideBySide(top, row, Slot::Row, rackWidth, i,
						  device.size.width(), device.units);
		} else {
			if (device.size.width() > shelf_width)
				result.tooWide << i;
			addSideBySide(top, shelf, Slot::Shelf, shelf_width, i,
						  device.size.width(),
						  int(std::ceil(device.size.height() / unit - 1e-6)));
		}
	}

		//Into frames
	QList<QList<Slot>> groups;
	int group_units = 0;
	for (const Slot &slot : top + amps) {
		if (groups.isEmpty()
			|| (group_units + slot.units > largest && !groups.last().isEmpty())) {
			groups.append(QList<Slot>());
			group_units = 0;
		}
		if (slot.kind == Slot::Blank && groups.last().isEmpty())
			continue;
		groups.last() << slot;
		group_units += slot.units;
	}
	for (QList<Slot> &group : groups)
		while (!group.isEmpty() && group.last().kind == Slot::Blank)
			group.removeLast();
	groups.removeIf([](const QList<Slot> &group) { return group.isEmpty(); });

	auto add = [&result](Placement::Kind kind, int frame, QPointF pos,
						 int units = 0, int device = -1) {
		Placement placement;
		placement.kind = kind;
		placement.frame = frame;
		placement.pos = pos;
		placement.units = units;
		placement.device = device;
		result.placements << placement;
	};

	for (int f = 0 ; f < groups.size() ; ++f) {
		const QList<Slot> &group = groups.at(f);
		int units = 0;
		for (const Slot &slot : group)
			units += slot.units;
		result.units += units;
		const int size = smallestHolding(frame_sizes, units);
		result.frames << size;
		const qreal x = f * framePitch;
		add(Placement::Frame, f, QPointF(x, 0), size);

		int split = 0;
		while (split < group.size() && !group.at(split).amp
			   && group.at(split).kind != Slot::Blank)
			++split;
		int amp_units = 0;
		for (int i = split ; i < group.size() ; ++i)
			amp_units += group.at(i).units;

		int y = 0;
		for (int i = 0 ; i < group.size() ; ++i) {
			const Slot &slot = group.at(i);
			if (i == split)
				y = std::max(y, (size - amp_units) * unit);
			const qreal bottom = y + slot.units * unit;
			switch (slot.kind) {
				case Slot::Single:
					add(Placement::Device, f, QPointF(x, y), 0, slot.items.first());
					break;
				case Slot::Blank:
					add(Placement::Blank, f, QPointF(x, y), slot.units);
					break;
				case Slot::Row: {
					qreal cursor = 0;
					for (int index : slot.items) {
						const Device &device = devices.at(index);
						add(Placement::Device, f,
							QPointF(x + cursor, bottom - device.units * unit), 0, index);
						cursor += device.size.width();
					}
					break;
				}
				case Slot::Shelf: {
					if (!shelf_sizes.isEmpty()) {
						const int shelf_units = smallestHolding(shelf_sizes, slot.units);
						add(Placement::Shelf, f,
							QPointF(x, bottom - shelf_units * unit), shelf_units);
					}
					qreal cursor = ear;
					for (int index : slot.items) {
						const Device &device = devices.at(index);
						add(Placement::Device, f,
							QPointF(x + cursor, bottom - device.size.height()), 0, index);
						cursor += device.size.width();
					}
					break;
				}
			}
			y += slot.units * unit;
		}
	}
	return result;
}
