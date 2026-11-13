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
#include "qetgraphicsitem/conductortextitem.h"
#include "qetgraphicsitem/element.h"
#include "qetgraphicsitem/terminal.h"
#include "qetproject.h"

#include <QDomDocument>
#include <QtTest>

/**
	A wire with text_at_ends shows its text beside both terminals; without
	it the single text stays in the middle, as before.
*/
class tst_textatends : public QObject
{
	Q_OBJECT

	struct Rig
	{
		QETProject project;
		Diagram *diagram = nullptr;
		Element *a = nullptr;
		Element *b = nullptr;

		Rig()
		{
			diagram = project.addNewDiagram();
			a = symbol(QStringLiteral("a"));
			b = symbol(QStringLiteral("b"));
			a->setPos(0, 0);
			b->setPos(0, 300);
			QCoreApplication::processEvents();
		}

		Element *symbol(const QString &name)
		{
			QDomDocument definition;
			definition.setContent(QStringLiteral(
				"<definition type=\"element\" link_type=\"simple\" uuid=\"%1\" "
				"width=\"60\" height=\"40\" hotspot_x=\"30\" hotspot_y=\"20\">"
				"<names><name lang=\"en\">%2</name></names>"
				"<description><line x1=\"0\" y1=\"-10\" x2=\"40\" y2=\"-10\"/>"
				"<terminal x=\"0\" y=\"20\" orientation=\"s\" uuid=\"%3\"/>"
				"</description></definition>")
				.arg(QUuid::createUuid().toString(), name,
					 QUuid::createUuid().toString()));
			if (!project.embeddedElementCollection()->addElementDefinition(
					QStringLiteral("import"), name,
					definition.documentElement()))
				return nullptr;
			int state = 0;
			Element *element = ElementFactory::Instance()->createElement(
				ElementsLocation(QStringLiteral("embed://import/%1.elmt")
									 .arg(name), &project),
				nullptr, &state);
			if (state || !element) return nullptr;
			diagram->addItem(element);
			return element;
		}

		Conductor *wire(bool atEnds)
		{
			auto *c = new Conductor(a->terminals().first(),
									b->terminals().first());
			diagram->addItem(c);
			ConductorProperties p = c->properties();
			p.text = QStringLiteral("SDI-001-03-07");
			p.m_text_at_ends = atEnds;
			c->setProperties(p);
			QCoreApplication::processEvents();
			return c;
		}
	};

		//Distance from a scene point to the scene rectangle of an item
	static qreal distance(const QPointF &p, const QGraphicsItem *item)
	{
		const QRectF r = item->sceneBoundingRect();
		const qreal dx = qMax(qMax(r.left() - p.x(), p.x() - r.right()), 0.0);
		const qreal dy = qMax(qMax(r.top() - p.y(), p.y() - r.bottom()), 0.0);
		return qSqrt(dx * dx + dy * dy);
	}

	private slots:
		void twoTextsNearEnds()
		{
			Rig r;
			Conductor *c = r.wire(true);
			ConductorTextItem *first = c->textItem();
			ConductorTextItem *second = c->endTextItem();
			QVERIFY(first && second);
			QVERIFY(first->isVisible() && second->isVisible());
			QCOMPARE(first->toPlainText(), QStringLiteral("SDI-001-03-07"));
			QCOMPARE(second->toPlainText(), first->toPlainText());
			QVERIFY(distance(c->terminal1->scenePos(), first) <= 30);
			QVERIFY(distance(c->terminal2->scenePos(), second) <= 30);
				//and not each near the other end
			QVERIFY(distance(c->terminal2->scenePos(), first) > 30);
		}

		void secondFollowsFirst()
		{
			Rig r;
			Conductor *c = r.wire(true);
			ConductorProperties p = c->properties();
			p.text = QStringLiteral("NET-002-03-07");
			p.text_color = Qt::red;
			p.text_size = 17;
			c->setProperties(p);
			QCOMPARE(c->endTextItem()->toPlainText(),
					 QStringLiteral("NET-002-03-07"));
			QCOMPARE(c->endTextItem()->defaultTextColor(), QColor(Qt::red));
			QCOMPARE(c->endTextItem()->font().pointSize(), 17);
			p.m_show_text = false;
			c->setProperties(p);
			QVERIFY(!c->textItem()->isVisible());
			QVERIFY(!c->endTextItem()->isVisible());
		}

		void offKeepsSingleMiddleText()
		{
			Rig r;
			Conductor *c = r.wire(false);
			QVERIFY(!c->endTextItem() || !c->endTextItem()->isVisible());
			QVERIFY(c->textItem()->isVisible());
				//middle of the wire: far from both terminals
			QVERIFY(distance(c->terminal1->scenePos(), c->textItem()) > 30);
			QVERIFY(distance(c->terminal2->scenePos(), c->textItem()) > 30);
				//switching the flag on and off again comes back
			ConductorProperties p = c->properties();
			const QPointF middle = c->textItem()->pos();
			p.m_text_at_ends = true;
			c->setProperties(p);
			QVERIFY(c->endTextItem() && c->endTextItem()->isVisible());
			p.m_text_at_ends = false;
			c->setProperties(p);
			QVERIFY(!c->endTextItem()->isVisible());
			QCOMPARE(c->textItem()->pos(), middle);
		}

		void secondTextIsNotSaved()
		{
			Rig r;
			Conductor *c = r.wire(true);
			QDomDocument doc;
			QHash<Terminal *, int> ids;
			QDomElement e = c->toXml(doc, ids);
			QVERIFY(!e.isNull());
			QCOMPARE(e.attribute("text_at_ends"), QStringLiteral("1"));
			QVERIFY(e.attribute("userx").isEmpty());
		}

		void flaggedWireKeepsBothTextsInFolioMode()
		{
			Rig r;
			r.diagram->defaultConductorProperties.m_one_text_per_folio = true;
			Conductor *c = r.wire(true);
			c->textItem()->setVisible(true);
				//a longer, unflagged wire of the same potential
			Element *far = r.symbol(QStringLiteral("far"));
			far->setPos(0, 900);
			auto *longer = new Conductor(r.b->terminals().first(),
										 far->terminals().first());
			r.diagram->addItem(longer);
			QCoreApplication::processEvents();
			longer->calculateTextItemPosition();
			c->calculateTextItemPosition();
			QVERIFY(c->textItem()->isVisible());
			QVERIFY(c->endTextItem()->isVisible());
		}

		void rotatingFirstRotatesSecond()
		{
			Rig r;
			Conductor *c = r.wire(true);
			c->textItem()->setRotation(90);
			c->textItem()->forceRotateByUser(true);
			QCOMPARE(c->endTextItem()->rotation(), qreal(90));
		}

		void shortWireShowsOneText()
		{
			Rig r;
			r.b->setPos(0, 40);
			QCoreApplication::processEvents();
			Conductor *c = r.wire(true);
			QVERIFY(c->textItem()->isVisible());
			QVERIFY2(!c->endTextItem()->isVisible(),
					 "two numbers that would touch: only the first stays");
				//long again: both come back
			r.b->setPos(0, 300);
			QCoreApplication::processEvents();
			QVERIFY(c->endTextItem()->isVisible());
		}

		void moveElementFollows()
		{
			Rig r;
			Conductor *c = r.wire(true);
			const QPointF before = c->textItem()->scenePos();
			r.a->setPos(100, 0);
			QCoreApplication::processEvents();
			const QPointF after = c->textItem()->scenePos();
			QVERIFY2(qAbs((after.x() - before.x()) - 100) < 1,
					 "first text must move with its element");
			QVERIFY(distance(c->terminal1->scenePos(), c->textItem()) <= 30);
			QVERIFY(distance(c->terminal2->scenePos(), c->endTextItem()) <= 30);
		}
};

QET_APP_TEST_MAIN(tst_textatends)
#include "tst_textatends.moc"
