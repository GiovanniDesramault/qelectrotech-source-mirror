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
#include "tecside/tecsidecablenumber.h"
#include "undocommand/addgraphicsobjectcommand.h"
#include "undocommand/changeelementinformationcommand.h"
#include "undocommand/deleteqgraphicsitemcommand.h"

#include <QDomDocument>
#include <QTemporaryDir>
#include <QUndoStack>
#include <QtTest>

using TecsideCableNumber::fromTo;
using Signal = TerminalData::Signal;

/**
	Live TYPE-seq-from-to numbers of single cables: the pure helpers, and
	the numbers a project hands out and keeps.
*/
class tst_tecsidecablenumber : public QObject
{
	Q_OBJECT

		//A project in AtSource mode with a camera (location 03, four
		//outputs, the fourth one HDM) and a switcher (location 07, four
		//SDI inputs)
	struct Rig
	{
		QETProject project;
		Diagram *diagram = nullptr;
		Element *camera = nullptr;
		Element *switcher = nullptr;

		Rig()
		{
			PotentialTypes types;
			types.setMode(PotentialTypes::Mode::AtSource);
			types.setTypes({{QStringLiteral("SDI"), QColor(0, 0, 255), Qt::SolidLine, 3},
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

		Element *symbol(const QString &name, const QString &signal, bool hdm)
		{
			auto *collection = project.embeddedElementCollection();
			QString terminals;
			for (int i = 0 ; i < 4 ; ++i) {
				const bool last = i == 3;
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

	private slots:
		void formatPads()
		{
			QCOMPARE(TecsideCableNumber::format(
						 QStringLiteral("SDI"), 12, QStringLiteral("3"),
						 QStringLiteral("7")),
					 QStringLiteral("SDI-012-03-07"));
		}

		void formatMissingLocation()
		{
			QCOMPARE(TecsideCableNumber::format(
						 QStringLiteral("NET"), 1, QString(),
						 QStringLiteral("05")),
					 QStringLiteral("NET-001-00-05"));
		}

		void formatWithoutCodeIsEmpty()
		{
			QVERIFY(TecsideCableNumber::format(
						QString(), 1, QStringLiteral("03"),
						QStringLiteral("07")).isEmpty());
		}

		void nextFreeFillsFirstGap()
		{
			QCOMPARE(TecsideCableNumber::nextFree({1, 2, 4}), 3);
			QCOMPARE(TecsideCableNumber::nextFree({}), 1);
			QCOMPARE(TecsideCableNumber::nextFree({1, 2, 3}), 4);
		}

		void fromToByOut()
		{
			const auto a = fromTo(QStringLiteral("07"), Signal::Out,
								  QStringLiteral("03"), Signal::In);
			QCOMPARE(a.first, QStringLiteral("07"));
			QCOMPARE(a.second, QStringLiteral("03"));
			const auto b = fromTo(QStringLiteral("07"), Signal::In,
								  QStringLiteral("03"), Signal::Out);
			QCOMPARE(b.first, QStringLiteral("03"));
			QCOMPARE(b.second, QStringLiteral("07"));
		}

		void fromToFallbackLowest()
		{
			const auto a = fromTo(QStringLiteral("07"), Signal::None,
								  QStringLiteral("03"), Signal::None);
			QCOMPARE(a.first, QStringLiteral("03"));
			QCOMPARE(a.second, QStringLiteral("07"));
		}

		void firstAndSecondSdi()
		{
			Rig r;
			Conductor *a = r.draw(r.out(0), r.in(0));
			Conductor *b = r.draw(r.out(1), r.in(1));
			QCOMPARE(a->properties().text, QStringLiteral("SDI-001-03-07"));
			QCOMPARE(b->properties().text, QStringLiteral("SDI-002-03-07"));
		}

		void fromIsOutputSideEvenWhenDrawnBackwards()
		{
			Rig r;
			Conductor *c = r.draw(r.in(0), r.out(0));
			QCOMPARE(c->properties().text, QStringLiteral("SDI-001-03-07"));
		}

		void typeChangeTakesNextOfNewType()
		{
			Rig r;
			Conductor *a = r.draw(r.out(0), r.in(0));
			Conductor *b = r.draw(r.out(1), r.in(1));
			r.setOwnType(a, QStringLiteral("NET"));
			QCOMPARE(a->properties().text, QStringLiteral("NET-001-03-07"));
			QCOMPARE(b->properties().text, QStringLiteral("SDI-002-03-07"));
		}

		void deleteLeavesGap()
		{
			Rig r;
			Conductor *a = r.draw(r.out(0), r.in(0));
			Conductor *b = r.draw(r.out(1), r.in(1));
			r.remove(a);
			Conductor *c = r.draw(r.out(2), r.in(2));
			QCOMPARE(c->properties().text, QStringLiteral("SDI-001-03-07"));
			QCOMPARE(b->properties().text, QStringLiteral("SDI-002-03-07"));
		}

		void untypedWireHasNoNumber()
		{
			Rig r;
			Conductor *c = r.draw(r.out(3), r.in(3));
			QVERIFY(c->properties().text.isEmpty());
			QCOMPARE(c->properties().m_type_seq, -1);
		}

		void unnumberedWireIsLeftAlone()
		{
			Rig r;
			Conductor *c = r.draw(r.out(0), r.in(0), false);
			QVERIFY(!c->properties().text.contains(QStringLiteral("SDI-")));
			QCOMPARE(c->properties().m_type_seq, -1);
		}

		void numbersSurviveSaveAndReload()
		{
			QTemporaryDir dir;
			QVERIFY(dir.isValid());
			const QString path = dir.filePath(QStringLiteral("numbers.qet"));
			QHash<QUuid, QPair<QString, int>> before;
			{
				Rig r;
				r.draw(r.out(0), r.in(0));
				r.draw(r.out(1), r.in(1));
				for (Conductor *c : r.diagram->conductors())
					before.insert(c->uuid(), {c->properties().text,
											  c->properties().m_type_seq});
				QCOMPARE(before.size(), 2);
				r.project.setFilePath(path);
				QVERIFY(r.project.write().isOk());
			}
			QETProject reopened(path);
			QHash<QUuid, QPair<QString, int>> after;
			for (Diagram *d : reopened.diagrams())
				for (Conductor *c : d->conductors())
					after.insert(c->uuid(), {c->properties().text,
											 c->properties().m_type_seq});
			QCOMPARE(after, before);
			QVERIFY(!after.isEmpty());
		}

		void undoRedoKeepsNumber()
		{
			Rig r;
			r.draw(r.out(0), r.in(0));
			r.draw(r.out(1), r.in(1));
			Conductor *c = r.draw(r.out(2), r.in(2));
			QCOMPARE(c->properties().text, QStringLiteral("SDI-003-03-07"));
			r.diagram->undoStack().undo();
			r.settle();
			r.diagram->undoStack().redo();
			r.settle();
			QCOMPARE(c->properties().text, QStringLiteral("SDI-003-03-07"));
			QCOMPARE(c->properties().m_type_seq, 3);
		}

		void locationChangeUpdatesText()
		{
			Rig r;
			Conductor *c = r.draw(r.out(0), r.in(0));
			const DiagramContext old = r.camera->elementInformations();
			DiagramContext info = old;
			info.addValue(QStringLiteral("location"), QStringLiteral("04"));
			r.diagram->undoStack().push(new ChangeElementInformationCommand(
				r.camera, old, info));
			r.settle();
			QCOMPARE(c->properties().text, QStringLiteral("SDI-001-04-07"));
		}

		void pastedCopyGetsNewNumber()
		{
			Rig r;
			Conductor *original = r.draw(r.out(0), r.in(0));
			QCOMPARE(original->properties().text,
					 QStringLiteral("SDI-001-03-07"));
				//A copy whose uuid sorts lower than the original's
			Conductor *copy = nullptr;
			for (int i = 0 ; i < 200 && !copy ; ++i) {
				auto *c = new Conductor(r.out(1), r.in(1));
				if (c->uuid().toString() < original->uuid().toString())
					copy = c;
				else
					delete c;
			}
			QVERIFY(copy);
			ConductorProperties p = copy->properties();
			p.m_tecside_number = true;
			p.m_type_seq = 1;
			p.m_type_seq_of = QStringLiteral("SDI");
			copy->setProperties(p);
			r.diagram->undoStack().push(
				new AddGraphicsObjectCommand(copy, r.diagram, QPointF()));
			r.settle();
			QCOMPARE(original->properties().text,
					 QStringLiteral("SDI-001-03-07"));
			QCOMPARE(copy->properties().text,
					 QStringLiteral("SDI-002-03-07"));
		}

		void duplicateSeqIsReassigned()
		{
			Rig r;
			Conductor *wires[2];
			for (int i = 0 ; i < 2 ; ++i) {
				auto *c = new Conductor(r.out(i), r.in(i));
				ConductorProperties p = c->properties();
				p.m_tecside_number = true;
				p.m_type_seq = 1;
				p.m_type_seq_of = QStringLiteral("SDI");
				c->setProperties(p);
				r.diagram->undoStack().push(
					new AddGraphicsObjectCommand(c, r.diagram, QPointF()));
				wires[i] = c;
			}
			r.settle();
			QSet<int> numbers{wires[0]->properties().m_type_seq,
							  wires[1]->properties().m_type_seq};
			QCOMPARE(numbers, (QSet<int>{1, 2}));
			Conductor *keeper = wires[0]->uuid().toString()
					< wires[1]->uuid().toString() ? wires[0] : wires[1];
			Conductor *other = keeper == wires[0] ? wires[1] : wires[0];
			QCOMPARE(keeper->properties().text, QStringLiteral("SDI-001-03-07"));
			QCOMPARE(other->properties().text, QStringLiteral("SDI-002-03-07"));
		}
};

QET_APP_TEST_MAIN(tst_tecsidecablenumber)
#include "tst_tecsidecablenumber.moc"
