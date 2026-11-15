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
#include "tecside/tecsidelocations.h"
#include "tecside/tecsidematerials.h"

#include <QDomDocument>
#include <QUndoStack>
#include <QtTest>

/**
	Materials list: devices per model and place, every physical cable
	once, per type and run.
*/
class tst_tecsidematerials : public QObject
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

		Element *device(const ElementsLocation &location, const QPointF &pos,
						const QString &where)
		{
			Element *element = place(location, pos);
			DiagramContext info = element->elementInformations();
			info.addValue(QETInformation::ELMT_LOCATION, where);
			element->setElementInformations(info);
			return element;
		}

		Conductor *typed(Terminal *a, Terminal *b, const QString &type)
		{
			Conductor *c = wire(a, b, QString());
			ConductorProperties p = c->properties();
			p.m_potential_type = type;
			c->setProperties(p);
			return c;
		}
	};

	static QString find(const QList<TecsideMaterials::Line> &lines, const QString &place,
						bool cable, const QString &item)
	{
		for (const auto &line : lines)
			if (line.place == place && line.cable == cable && line.item == item)
				return QString::number(line.count);
		return QStringLiteral("missing");
	}

	private slots:
		void countsPerPlaceAndRun()
		{
			Rig r;
			TecsideLocations locations;
			locations.setEnabled(true);
			locations.setList({{QStringLiteral("01"), QStringLiteral("FOH")},
							   {QStringLiteral("02"), QStringLiteral("Stage")}});
			r.project.setTecsideLocations(locations);
			const ElementsLocation cam = r.define("cam", {"SDI OUT"}, "Cam");
			const ElementsLocation conv = r.define("conv", {"SDI IN", "SDI IN 2", "HDMI OUT"}, "Conv");
			const ElementsLocation tv = r.define("tv", {"HDMI IN"}, "TV");
			Element *cam1 = r.device(cam, {-200, 0}, "01");
			Element *cam2 = r.device(cam, {-200, 100}, "01");
			Element *c = r.device(conv, {0, 0}, "01");
			Element *t = r.device(tv, {200, 0}, "02");
			r.place(tv, {200, 200});	//no place

			r.typed(Rig::terminal(cam1, "SDI OUT"), Rig::terminal(c, "SDI IN"), "S");
			r.typed(Rig::terminal(cam2, "SDI OUT"), Rig::terminal(c, "SDI IN 2"), "S");
			r.typed(Rig::terminal(c, "HDMI OUT"), Rig::terminal(t, "HDMI IN"), "HDMI");

			const auto lines = TecsideMaterials::lines(&r.project);
			QCOMPARE(find(lines, "01 – FOH", false, "Cam"), QStringLiteral("2"));
			QCOMPARE(find(lines, "01 – FOH", false, "Conv"), QStringLiteral("1"));
			QCOMPARE(find(lines, "02 – Stage", false, "TV"), QStringLiteral("1"));
			QCOMPARE(find(lines, "No location", false, "TV"), QStringLiteral("1"));
			QCOMPARE(find(lines, "01 – FOH", true, "SDI"), QStringLiteral("2"));
			QCOMPARE(find(lines, "01 – FOH ↔ 02 – Stage", true, "HDMI"), QStringLiteral("1"));
				//places in the order of the list, the runs after, no place last
			QCOMPARE(lines.first().place, QStringLiteral("01 – FOH"));
			QVERIFY(!lines.first().cable);
			QCOMPARE(lines.last().place, QStringLiteral("No location"));

			const QByteArray csv = TecsideMaterials::toCsv(&r.project);
			QVERIFY(csv.contains("\"01 – FOH\";\"Cable\";\"SDI\";\"2\""));

			Diagram *sheet = TecsideMaterials::draw(&r.project);
			QVERIFY(sheet);
			QVERIFY(r.project.folios().contains(sheet));
		}
};

QET_APP_TEST_MAIN(tst_tecsidematerials)
#include "tst_tecsidematerials.moc"
