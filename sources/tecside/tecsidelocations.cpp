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
#include "tecside/tecsidelocations.h"

#include "bordertitleblock.h"
#include "diagram.h"
#include "qetapp.h"
#include "qetgraphicsitem/element.h"
#include "qetinformation.h"
#include "qetproject.h"
#include "undocommand/changeelementinformationcommand.h"

#include <QCoreApplication>
#include <QDomDocument>
#include <QFontMetricsF>
#include <QPainter>
#include <QSet>

#include <algorithm>

const QString TecsideLocations::xmlTagName = QStringLiteral("tecside_locations");

namespace
{
	const qreal row_height = 12.0;
	const qreal min_width = 160.0;
	const qreal padding = 3.0;

	QFont boxFont(bool bold = false)
	{
		QFont font = QETApp::diagramTextsFont(7);
		font.setBold(bold);
		return font;
	}

	QString header()
	{
		return QCoreApplication::translate("TecsideLocations", "Locations",
										   "header of the box of places on every sheet");
	}

		//The widths of the number column and of the whole box
	std::pair<qreal, qreal> widths(const QList<TecsideLocation> &list)
	{
		const QFontMetricsF metrics(boxFont());
		qreal number = metrics.horizontalAdvance(QStringLiteral("00"));
		qreal name = 0;
		for (const TecsideLocation &location : list) {
			number = std::max(number, metrics.horizontalAdvance(location.number));
			name = std::max(name, metrics.horizontalAdvance(location.name));
		}
		number += 2 * padding;
		const qreal head = QFontMetricsF(boxFont(true)).horizontalAdvance(header())
				+ 2 * padding;
		return {number, std::max({min_width, number + name + 2 * padding, head})};
	}
}

/**
	@return the name of the place numbered @p number, empty if none
*/
QString TecsideLocations::nameOf(const QString &number) const
{
	for (const TecsideLocation &location : m_list)
		if (location.number == number)
			return location.name;
	return QString();
}

/**
	@return how @p location is offered in a list: "03 – FOH"
*/
QString TecsideLocations::label(const TecsideLocation &location) const
{
	if (location.name.isEmpty())
		return location.number;
	return QStringLiteral("%1 – %2").arg(location.number, location.name);
}

/**
	@return the lowest two-digit number no place has yet, from 01
*/
QString TecsideLocations::nextFreeNumber() const
{
	QSet<QString> used;
	for (const TecsideLocation &location : m_list)
		used.insert(normalizedNumber(location.number));
	for (int i = 1 ; ; ++i) {
		const QString number = normalizedNumber(QString::number(i));
		if (!used.contains(number))
			return number;
	}
}

/**
	@return @p number trimmed, and padded to two digits when it is a
	single digit, as the cable numbers write it ("3" is "03")
*/
QString TecsideLocations::normalizedNumber(const QString &number)
{
	const QString trimmed = number.trimmed();
	bool is_number = false;
	trimmed.toInt(&is_number);
	if (is_number && trimmed.size() < 2)
		return trimmed.rightJustified(2, QLatin1Char('0'));
	return trimmed;
}

/**
	@return the places saved in @p element; off when @p element is null
	(a project without <tecside_locations>)
*/
TecsideLocations TecsideLocations::fromXml(const QDomElement &element)
{
	TecsideLocations locations;
	if (element.isNull() || element.tagName() != xmlTagName)
		return locations;
	locations.m_enabled = true;
	for (QDomElement child = element.firstChildElement(QStringLiteral("location"))
		 ; !child.isNull()
		 ; child = child.nextSiblingElement(QStringLiteral("location"))) {
		locations.m_list.append({child.attribute(QStringLiteral("number")),
								 child.attribute(QStringLiteral("name"))});
	}
	return locations;
}

QDomElement TecsideLocations::toXml(QDomDocument &document) const
{
	QDomElement element = document.createElement(xmlTagName);
	for (const TecsideLocation &location : m_list) {
		QDomElement child = document.createElement(QStringLiteral("location"));
		child.setAttribute(QStringLiteral("number"), location.number);
		child.setAttribute(QStringLiteral("name"), location.name);
		element.appendChild(child);
	}
	return element;
}

int TecsideLocations::boxRowCount(int count)
{
	return std::max(5, count);
}

/**
	@return the rows of the box, "NN Name" each, empty past the list
*/
QStringList TecsideLocations::boxRows() const
{
	QStringList rows;
	for (const TecsideLocation &location : m_list)
		rows << QStringLiteral("%1 %2").arg(location.number, location.name).trimmed();
	while (rows.size() < boxRowCount(count()))
		rows << QString();
	return rows;
}

/**
	@return where the box stands on a sheet framed by @p border: right
	above the title block, its right side on the title block's. With the
	title block on the right edge, left of it at the bottom; without
	one, in the bottom right corner of the drawing area.
*/
QRectF TecsideLocations::boxRect(const BorderTitleBlock &border) const
{
	const qreal width = widths(m_list).second;
	const qreal height = row_height * (1 + boxRowCount(count()));
	QPointF corner = border.insideBorderRect().bottomRight();
	if (border.titleBlockIsDisplayed()) {
		const QRectF title_block = border.titleBlockRect();
		corner = border.titleBlockEdge() == Qt::BottomEdge
				? title_block.topRight() : title_block.bottomLeft();
	}
	return QRectF(corner.x() - width, corner.y() - height, width, height);
}

/**
	Draw the box of the places on a sheet framed by @p border, lines and
	texts in @p ink on @p paper. Called where the title block is drawn
	(Diagram::drawBackground()), so the view, printing, the PDF and the
	picture exports all show it.
*/
void TecsideLocations::drawBox(QPainter *painter, const BorderTitleBlock &border,
							   const QColor &ink, const QColor &paper) const
{
	if (!m_enabled)
		return;
	const QRectF rect = boxRect(border);
	const qreal number_width = widths(m_list).first;
	const QStringList rows = boxRows();

	painter->save();
	painter->setPen(QPen(ink, 0));
	painter->setBrush(paper);
	painter->drawRect(rect);
	painter->setBrush(Qt::NoBrush);

	QRectF head(rect.left(), rect.top(), rect.width(), row_height);
	painter->setFont(boxFont(true));
	painter->drawText(head.adjusted(padding, 0, -padding, 0),
					  Qt::AlignVCenter | Qt::AlignLeft, header());
	painter->drawLine(head.bottomLeft(), head.bottomRight());

	painter->setFont(boxFont());
	const qreal split = rect.left() + number_width;
	painter->drawLine(QPointF(split, head.bottom()), rect.bottomLeft() + QPointF(number_width, 0));
	for (int i = 0 ; i < rows.size() ; ++i) {
		const qreal top = head.bottom() + i * row_height;
		if (i)
			painter->drawLine(QPointF(rect.left(), top), QPointF(rect.right(), top));
		if (i >= m_list.size())
			continue;
		const TecsideLocation &location = m_list.at(i);
		painter->drawText(QRectF(rect.left() + padding, top,
								 number_width - 2 * padding, row_height),
						  Qt::AlignVCenter | Qt::AlignLeft, location.number);
		painter->drawText(QRectF(split + padding, top,
								 rect.right() - split - 2 * padding, row_height),
						  Qt::AlignVCenter | Qt::AlignLeft, location.name);
	}
	painter->restore();
}

ChangeTecsideLocationsCommand::ChangeTecsideLocationsCommand(
		QETProject *project, const TecsideLocations &old_locations,
		const TecsideLocations &new_locations, QUndoCommand *parent) :
	QUndoCommand(parent),
	m_project(project),
	m_old(old_locations),
	m_new(new_locations)
{
	setText(QCoreApplication::translate("TecsideLocations", "Change the locations"));
}

void ChangeTecsideLocationsCommand::undo()
{
	if (m_project)
		m_project->setTecsideLocations(m_old);
	QUndoCommand::undo();
}

void ChangeTecsideLocationsCommand::redo()
{
	if (m_project)
		m_project->setTecsideLocations(m_new);
	QUndoCommand::redo();
}

namespace TecsideLocationEdit
{
/**
	@return the command giving each of @p elements the location
	@p location, a child of @p parent; nullptr when none changes
*/
QUndoCommand *setLocation(const QList<Element *> &elements,
						  const QString &location, QUndoCommand *parent)
{
	QMap<QPointer<Element>, QPair<DiagramContext, DiagramContext>> changes;
	for (Element *element : elements) {
		if (!element)
			continue;
		const DiagramContext old_info = element->elementInformations();
		if (old_info.value(QETInformation::ELMT_LOCATION).toString() == location)
			continue;
		DiagramContext new_info = old_info;
		new_info.addValue(QETInformation::ELMT_LOCATION, location);
		changes.insert(element, qMakePair(old_info, new_info));
	}
	if (changes.isEmpty())
		return nullptr;
	auto *command = new ChangeElementInformationCommand(changes, parent);
	command->setText(QCoreApplication::translate("TecsideLocations", "Set the location"));
	return command;
}

/**
	@return the command moving every element of @p project whose location
	is a key of @p old_to_new to the value, a child of @p parent; nullptr
	when no element has one. The folios not built yet are built first.
	Each element moves from the location it had, so two places can swap
	their numbers.
*/
QUndoCommand *renumber(QETProject *project, const QHash<QString, QString> &old_to_new,
					   QUndoCommand *parent)
{
	if (!project || old_to_new.isEmpty())
		return nullptr;
	QMap<QPointer<Element>, QPair<DiagramContext, DiagramContext>> changes;
	for (Diagram *folio : project->folios()) {
		project->buildFolioToShow(folio);
		folio->ensureLoaded();
		for (Element *element : folio->elements()) {
			const DiagramContext old_info = element->elementInformations();
			const QString old_number = old_info.value(QETInformation::ELMT_LOCATION).toString();
			if (!old_to_new.contains(old_number))
				continue;
			DiagramContext new_info = old_info;
			new_info.addValue(QETInformation::ELMT_LOCATION, old_to_new.value(old_number));
			changes.insert(element, qMakePair(old_info, new_info));
		}
	}
	if (changes.isEmpty())
		return nullptr;
	auto *command = new ChangeElementInformationCommand(changes, parent);
	command->setText(QCoreApplication::translate("TecsideLocations", "Renumber the locations"));
	return command;
}
}
