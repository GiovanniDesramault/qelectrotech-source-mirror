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
#include "conductorproperties.h"
#include "diagram.h"
#include "factory/elementfactory.h"
#include "qetapptest.h"
#include "qetgraphicsitem/conductor.h"
#include "qetgraphicsitem/element.h"
#include "qetgraphicsitem/terminal.h"
#include "qetinformation.h"
#include "qetproject.h"
#include "tecside/tecsidereplace.h"

#include <QDomDocument>
#include <QUndoStack>
#include <QtTest>

/**
	Replace with: the new symbol takes the place of the old one, each wire
	goes to the terminal of the same name, one undo step.
*/
class tst_tecsidereplace : public QObject
{
	Q_OBJECT

	struct Rig
	{
		QETProject project;
		Diagram *diagram = nullptr;

		Rig() {diagram = project.addNewDiagram();}

			//A symbol with one terminal per name, in the embedded collection
		ElementsLocation define(const QString &name, const QStringList &terminals,
								const QString &manufacturer)
		{
			QString parts;
			int y = 0;
			for (const QString &terminal : terminals)
				parts += QStringLiteral("<terminal x=\"0\" y=\"%1\" orientation=\"w\" "
										"name=\"%2\" uuid=\"%3\"/>")
						 .arg(y += 10).arg(terminal, QUuid::createUuid().toString());
			QDomDocument definition;
			definition.setContent(QStringLiteral(
				"<definition type=\"element\" link_type=\"simple\" uuid=\"%1\" "
				"width=\"60\" height=\"60\" hotspot_x=\"30\" hotspot_y=\"0\">"
				"<names><name lang=\"en\">%2</name></names>"
				"<elementInformations><elementInformation show=\"1\" "
				"name=\"manufacturer\">%3</elementInformation></elementInformations>"
				"<description><line x1=\"0\" y1=\"0\" x2=\"40\" y2=\"0\"/>%4"
				"</description></definition>")
				.arg(QUuid::createUuid().toString(), name, manufacturer, parts));
			project.embeddedElementCollection()->addElementDefinition(
				QStringLiteral("import"), name, definition.documentElement());
			return ElementsLocation(QStringLiteral("embed://import/%1.elmt").arg(name),
									&project);
		}

		Element *place(const ElementsLocation &location, const QPointF &pos)
		{
			int state = 0;
			Element *element = ElementFactory::Instance()->createElement(location, nullptr, &state);
			if (state || !element) return nullptr;
			diagram->addItem(element);
			element->setPos(pos);
			return element;
		}

		static Terminal *terminal(Element *element, const QString &name)
		{
			for (Terminal *t : element->terminals())
				if (t->name() == name)
					return t;
			return nullptr;
		}

		Conductor *wire(Terminal *a, Terminal *b, const QString &text)
		{
			auto *c = new Conductor(a, b);
			diagram->addItem(c);
			ConductorProperties p = c->properties();
			p.text = text;
			c->setProperties(p);
			return c;
		}
	};

	private slots:
		void wiresFollowByName()
		{
			Rig r;
			Element *old = r.place(r.define("conv_a", {"SDI IN", "HDMI OUT"}, "Generic"), {0, 0});
			Element *cam = r.place(r.define("cam", {"SDI OUT"}, "Cam"), {-200, 0});
			Element *tv = r.place(r.define("tv", {"HDMI IN"}, "TV"), {200, 0});
			QVERIFY(old && cam && tv);
			r.wire(Rig::terminal(cam, "SDI OUT"), Rig::terminal(old, "SDI IN"), "SDI-001");
			r.wire(Rig::terminal(old, "HDMI OUT"), Rig::terminal(tv, "HDMI IN"), "HDMI-001");
			DiagramContext info = old->elementInformations();
			info.addValue(QETInformation::ELMT_LABEL, "CONV1");
			info.addValue(QETInformation::ELMT_LOCATION, "02");
			old->setElementInformations(info);

				//the new one has its terminals in another order, and one more
			QStringList lost;
			Element *now = TecsideReplace::replace(
				old, r.define("conv_b", {"HDMI OUT", "SDI LOOP", "SDI IN"}, "Blackmagic"), &lost);
			QVERIFY(now);
			QVERIFY(lost.isEmpty());
			QVERIFY(!r.diagram->elements().contains(old));
			QCOMPARE(now->pos(), QPointF(0, 0));

			Terminal *sdi = Rig::terminal(now, "SDI IN");
			Terminal *hdmi = Rig::terminal(now, "HDMI OUT");
			QCOMPARE(sdi->conductors().size(), 1);
			QCOMPARE(hdmi->conductors().size(), 1);
			QCOMPARE(Rig::terminal(now, "SDI LOOP")->conductors().size(), 0);
			Conductor *in = sdi->conductors().first();
			QCOMPARE(in->terminal1, Rig::terminal(cam, "SDI OUT"));
			QCOMPARE(in->properties().text, QStringLiteral("SDI-001"));
			QCOMPARE(hdmi->conductors().first()->terminal2, Rig::terminal(tv, "HDMI IN"));

			const DiagramContext after = now->elementInformations();
			QCOMPARE(after.value(QETInformation::ELMT_LABEL).toString(), QStringLiteral("CONV1"));
			QCOMPARE(after.value(QETInformation::ELMT_LOCATION).toString(), QStringLiteral("02"));
			QCOMPARE(after.value("manufacturer").toString(), QStringLiteral("Blackmagic"));
		}

		void wireWithoutNamesakeIsReported()
		{
			Rig r;
			Element *old = r.place(r.define("conv_a", {"SDI IN", "HDMI OUT"}, "Generic"), {0, 0});
			Element *tv = r.place(r.define("tv", {"HDMI IN"}, "TV"), {200, 0});
			r.wire(Rig::terminal(old, "HDMI OUT"), Rig::terminal(tv, "HDMI IN"), "HDMI-001");
			QStringList lost;
			Element *now = TecsideReplace::replace(old, r.define("sdi_only", {"SDI IN"}, "X"), &lost);
			QVERIFY(now);
			QCOMPARE(lost, QStringList{QStringLiteral("HDMI OUT")});
			QCOMPARE(Rig::terminal(tv, "HDMI IN")->conductors().size(), 0);
		}

		void undoBringsTheOldOneBack()
		{
			Rig r;
			Element *old = r.place(r.define("conv_a", {"SDI IN"}, "Generic"), {0, 0});
			Element *cam = r.place(r.define("cam", {"SDI OUT"}, "Cam"), {-200, 0});
			r.wire(Rig::terminal(cam, "SDI OUT"), Rig::terminal(old, "SDI IN"), "SDI-001");
			Element *now = TecsideReplace::replace(old, r.define("conv_b", {"SDI IN"}, "B"));
			QVERIFY(now);
			r.project.undoStack()->undo();
			QVERIFY(r.diagram->elements().contains(old));
			QVERIFY(!r.diagram->elements().contains(now));
			QCOMPARE(Rig::terminal(old, "SDI IN")->conductors().size(), 1);
			QCOMPARE(Rig::terminal(cam, "SDI OUT")->conductors().size(), 1);
			r.project.undoStack()->redo();
			QVERIFY(!r.diagram->elements().contains(old));
			QCOMPARE(Rig::terminal(now, "SDI IN")->conductors().size(), 1);
		}
};

QET_APP_TEST_MAIN(tst_tecsidereplace)
#include "tst_tecsidereplace.moc"
