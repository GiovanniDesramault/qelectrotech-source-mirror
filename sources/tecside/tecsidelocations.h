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
#ifndef TECSIDELOCATIONS_H
#define TECSIDELOCATIONS_H

#include <QHash>
#include <QList>
#include <QPointer>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QUndoCommand>

class BorderTitleBlock;
class Element;
class QColor;
class QDomDocument;
class QDomElement;
class QPainter;
class QETProject;

/**
	@brief One place of a TECside project: the number its devices carry
	in their "location" information (and so in the cable numbers), and
	what the place is called.
*/
struct TecsideLocation
{
	QString number;
	QString name;

	bool operator==(const TecsideLocation &other) const
	{return number == other.number && name == other.name;}
	bool operator!=(const TecsideLocation &other) const
	{return !(*this == other);}
};

/**
	@brief The ordered list of the places of a project, saved as
	<tecside_locations> under the project root. The element there, even
	empty, turns the feature on; a project without it is left exactly as
	it was (no box on the sheets, a plain location field).
*/
class TecsideLocations
{
	public:
		static const QString xmlTagName;

		bool isEnabled() const {return m_enabled;}
		void setEnabled(bool enabled) {m_enabled = enabled;}
		const QList<TecsideLocation> &list() const {return m_list;}
		void setList(const QList<TecsideLocation> &list) {m_list = list;}
		int count() const {return m_list.count();}

		QString nameOf(const QString &number) const;
		QString label(const TecsideLocation &location) const;
		QString nextFreeNumber() const;
		static QString normalizedNumber(const QString &number);

		static TecsideLocations fromXml(const QDomElement &element);
		QDomElement toXml(QDomDocument &document) const;

			/// The rows of the box on the sheets: max(5, count()), the
			/// ones past the list empty
		static int boxRowCount(int count);
		QStringList boxRows() const;
		QRectF boxRect(const BorderTitleBlock &border) const;
		void drawBox(QPainter *painter, const BorderTitleBlock &border,
					 const QColor &ink, const QColor &paper) const;

		bool operator==(const TecsideLocations &other) const
		{return m_enabled == other.m_enabled && m_list == other.m_list;}
		bool operator!=(const TecsideLocations &other) const
		{return !(*this == other);}

	private:
		bool m_enabled = false;
		QList<TecsideLocation> m_list;
};

/**
	@brief The undo command of a new list of places of @p project
*/
class ChangeTecsideLocationsCommand : public QUndoCommand
{
	public:
		ChangeTecsideLocationsCommand(QETProject *project,
									  const TecsideLocations &old_locations,
									  const TecsideLocations &new_locations,
									  QUndoCommand *parent = nullptr);
		void undo() override;
		void redo() override;

	private:
		QPointer<QETProject> m_project;
		TecsideLocations m_old, m_new;
};

namespace TecsideLocationEdit
{
	QUndoCommand *setLocation(const QList<Element *> &elements,
							  const QString &location,
							  QUndoCommand *parent = nullptr);
	QUndoCommand *renumber(QETProject *project,
						   const QHash<QString, QString> &old_to_new,
						   QUndoCommand *parent = nullptr);
}

#endif
