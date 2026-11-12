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
#include "ElementsCollection/elementslocation.h"
#include "ElementsCollection/xmlelementcollection.h"
#include "QPropertyUndoCommand/qpropertyundocommand.h"
#include "cable/cable.h"
#include "conductorproperties.h"
#include "diagram.h"
#include "diagramcontent.h"
#include "diagramcontext.h"
#include "factory/elementfactory.h"
#include "properties/potentialtypes.h"
#include "properties/terminaldata.h"
#include "qetapptest.h"
#include "qetgraphicsitem/conductor.h"
#include "qetgraphicsitem/element.h"
#include "qetgraphicsitem/terminal.h"
#include "qetproject.h"
#include "qetresult.h"
#include "bomexport.h"
#include "cablelist/cablelistrows.h"
#include "tecside/tecsidecablenumber.h"
#include "undocommand/addgraphicsobjectcommand.h"
#include "undocommand/changeelementinformationcommand.h"
#include "undocommand/deleteqgraphicsitemcommand.h"

#include <QDomDocument>
#include <QTemporaryDir>
#include <QUndoStack>
#include <QtTest>

using Signal = TerminalData::Signal;

/**
	The cable list CSV of a TECside project: type, number and both ends of
	every single cable and multicore, cores under their multicore; and the
	list of a project without TECside numbers, left as it always was.
*/
class tst_tecsideexport : public QObject
{
	Q_OBJECT

	struct Rig
	{
		QETProject project;
		Diagram *diagram = nullptr;
		Element *camera = nullptr;
		Element *switcher = nullptr;

		Rig(int terminalCount = 4) : m_terminals(terminalCount)
		{
			PotentialTypes types;
			types.setMode(PotentialTypes::Mode::AtSource);
			types.setTypes({{QStringLiteral("SDI"), QColor(0, 0, 255), Qt::SolidLine, 3},
							{QStringLiteral("AUD"), QColor(255, 128, 0), Qt::SolidLine, 3},
							{QStringLiteral("HDM"), QColor(0, 160, 0), Qt::SolidLine, 3},
							{QStringLiteral("NET"), QColor(200, 0, 0), Qt::SolidLine, 3}});
			project.setPotentialTypes(types);
			diagram = project.addNewDiagram();
			camera = symbol(QStringLiteral("camera"), QStringLiteral("out"), true);
			switcher = symbol(QStringLiteral("switcher"), QStringLiteral("in"), false);
			camera->setPos(0, 0);
			switcher->setPos(0, 200);
			setLocation(camera, QStringLiteral("03"));
			setLocation(switcher, QStringLiteral("07"));
			settle();
		}

		int m_terminals = 4;

		Element *symbol(const QString &name, const QString &signal, bool hdm)
		{
			auto *collection = project.embeddedElementCollection();
			QString terminals;
			for (int i = 0 ; i < m_terminals ; ++i) {
				const bool last = i == m_terminals - 1;
				terminals += QStringLiteral(
					"<terminal x=\"%1\" y=\"20\" orientation=\"s\" uuid=\"%2\" "
					"potential_type=\"%3\" signal=\"%4\"/>")
					.arg(i * 10).arg(QUuid::createUuid().toString(),
									 hdm && last ? QStringLiteral("HDM")
												 : QStringLiteral("SDI"),
									 signal);
			}
			QDomDocument definition;
			definition.setContent(QStringLiteral(
				"<definition type=\"element\" link_type=\"simple\" uuid=\"%1\" "
				"width=\"60\" height=\"40\" hotspot_x=\"30\" hotspot_y=\"20\">"
				"<names><name lang=\"en\">%2</name></names>"
				"<description><line x1=\"0\" y1=\"-10\" x2=\"40\" y2=\"-10\"/>%3"
				"</description></definition>")
				.arg(QUuid::createUuid().toString(), name, terminals));
			if (!collection->addElementDefinition(QStringLiteral("import"),
					name, definition.documentElement()))
				return nullptr;
			int state = 0;
			Element *element = ElementFactory::Instance()->createElement(
				ElementsLocation(QStringLiteral("embed://import/%1.elmt").arg(name),
								 &project),
				nullptr, &state);
			if (state || !element) return nullptr;
			diagram->addItem(element);
			return element;
		}

		static void setLocation(Element *element, const QString &location)
		{
			DiagramContext info = element->elementInformations();
			info.addValue(QStringLiteral("location"), location);
			element->setElementInformations(info);
		}

		void settle() { QCoreApplication::processEvents(); }

			//Out-of-undo edits ask for a refresh, as an undo command does
		void settleEdit()
		{
			project.scheduleDerivedPotentialTypes();
			settle();
		}

		Terminal *out(int i) const { return camera->terminals().at(i); }
		Terminal *in(int i) const { return switcher->terminals().at(i); }

		Conductor *draw(Terminal *a, Terminal *b, bool numbered = true)
		{
			auto *c = new Conductor(a, b);
			ConductorProperties p = c->properties();
			p.m_tecside_number = numbered;
			c->setProperties(p);
			diagram->undoStack().push(
				new AddGraphicsObjectCommand(c, diagram, QPointF()));
			settle();
			return c;
		}

			//A numbered wire of its own type, a core of @p cable when given
		Conductor *drawAs(Terminal *a, Terminal *b, const QString &type,
						  Cable *cable = nullptr, int slot = -1)
		{
			auto *c = new Conductor(a, b);
			ConductorProperties p = c->properties();
			p.m_tecside_number = true;
			p.m_potential_type = type;
			c->setProperties(p);
			diagram->undoStack().push(
				new AddGraphicsObjectCommand(c, diagram, QPointF()));
			settle();
			if (cable) {
					//The cable claims the wire, as the app does; the
					//reference on the wire follows from that
				CableCore core;
				core.core = slot;
				core.conductor = c->uuid();
				cable->setCore(core);
				settleEdit();
			}
			return c;
		}

		void setOwnType(Conductor *c, const QString &type)
		{
			const ConductorProperties old = c->properties();
			ConductorProperties p = old;
			p.m_potential_type = type;
			diagram->undoStack().push(new QPropertyUndoCommand(
				c, "properties", QVariant::fromValue(old),
				QVariant::fromValue(p)));
			settle();
		}

		void remove(Conductor *c)
		{
			c->setSelected(true);
			DiagramContent content(diagram);
			diagram->undoStack().push(
				new DeleteQGraphicsItemCommand(diagram, content));
			settle();
		}
	};

	static void setLabel(Element *element, const QString &label)
	{
		DiagramContext info = element->elementInformations();
		info.addValue(QStringLiteral("label"), label);
		element->setElementInformations(info);
	}

	static QStringList lines(const QByteArray &csv)
	{
		QString text = QString::fromUtf8(csv);
		if (text.startsWith(QChar(0xFEFF))) text.remove(0, 1);
		return text.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
	}

	static QString record(const QStringList &values)
	{
		return QString::fromUtf8(BomExport::csvRecord(values)).trimmed();
	}

	private slots:
		void tecsideRows()
		{
			Rig r(14);
			Cable *cable = r.project.newCable();
			cable->setCoreCount(12);
			setLabel(r.camera, QStringLiteral("CAM1"));
			setLabel(r.switcher, QStringLiteral("SW1"));
			Conductor *single = r.drawAs(r.out(0), r.in(0),
										 QStringLiteral("AUD"));
			for (int i = 0 ; i < 12 ; ++i)
				r.drawAs(r.out(i + 1), r.in(i + 1),
						 QStringLiteral("AUD"), cable, i);
			QCOMPARE(single->properties().text,
					 QStringLiteral("AUD-001-03-07"));

			const QStringList rows = lines(CableList::toCsv(&r.project));
			QCOMPARE(rows.size(), 1 + 1 + 1 + 12);
			QCOMPARE(rows.at(0), record({
				QStringLiteral("Type"), QStringLiteral("Number"),
				QStringLiteral("From device"),
				QStringLiteral("From connector"),
				QStringLiteral("From location"),
				QStringLiteral("To device"),
				QStringLiteral("To connector"),
				QStringLiteral("To location")}));
			QCOMPARE(rows.at(1), record({
				QStringLiteral("AUD"), QStringLiteral("AUD-001-03-07"),
				QStringLiteral("CAM1"), r.out(0)->name(),
				QStringLiteral("03"), QStringLiteral("SW1"),
				r.in(0)->name(), QStringLiteral("07")}));
			QCOMPARE(rows.at(2), record({
				QStringLiteral("AUD"), QStringLiteral("AUD-002-03-07"),
				QStringLiteral("CAM1"), r.out(1)->name(),
				QStringLiteral("03"), QStringLiteral("SW1"),
				r.in(1)->name(), QStringLiteral("07")}));
			QCOMPARE(rows.at(3), record({
				QStringLiteral("  "), QStringLiteral("AUD-002-03-07/1"),
				QStringLiteral("CAM1"), r.out(1)->name(),
				QStringLiteral("03"), QStringLiteral("SW1"),
				r.in(1)->name(), QStringLiteral("07")}));
			QVERIFY(rows.at(14).contains(
				QStringLiteral("AUD-002-03-07/12")));
		}

		void plainProjectIsUnchanged()
		{
			Rig r(5);
			Cable *cable = r.project.newCable();
			cable->setCoreCount(2);
			for (int i = 0 ; i < 2 ; ++i) {
				Conductor *c = r.draw(r.out(i), r.in(i), false);
				CableCore core;
				core.core = i;
				core.conductor = c->uuid();
				cable->setCore(core);
			}
			r.draw(r.out(3), r.in(3), false);
			r.settleEdit();

				//The list as it was written before TECside existed
			QByteArray expected("\xEF\xBB\xBF");
			QStringList headers;
			for (const auto &column : CableList::allColumns())
				headers << column.label;
			expected += BomExport::csvRecord(headers);
			const auto rows = CableList::rows(&r.project);
			QCOMPARE(rows.size(), 1);
			for (const auto &row : rows) {
				QStringList values;
				for (const auto &column : CableList::allColumns())
					values << row.value(column.key);
				expected += BomExport::csvRecord(values);
			}
			QCOMPARE(CableList::toCsv(&r.project), expected);
			QVERIFY(!CableList::hasTecsideNumbers(&r.project));
		}
};

QET_APP_TEST_MAIN(tst_tecsideexport)
#include "tst_tecsideexport.moc"
