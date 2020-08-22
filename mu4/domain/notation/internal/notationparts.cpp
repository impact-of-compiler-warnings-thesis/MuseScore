//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  Copyright (C) 2020 MuseScore BVBA and others
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FIT-0NESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program; if not, write to the Free Software
//  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
//=============================================================================
#include "notationparts.h"

#include "libmscore/score.h"
#include "libmscore/undo.h"
#include "libmscore/excerpt.h"

#include "log.h"

using namespace mu::domain::notation;
using namespace mu::async;

NotationParts::NotationParts(IGetScore* getScore)
    : m_getScore(getScore)
{
}

Ms::Score* NotationParts::score() const
{
    return m_getScore->score();
}

Ms::MasterScore* NotationParts::masterScore() const
{
    return score()->masterScore();
}

PartList NotationParts::parts() const
{
    PartList result;

    PartList parts;
    parts << scoreParts(score()) << excerptParts(score());

    QSet<QString> partIds;
    for (Part* part: parts) {
        if (partIds.contains(part->id())) {
            continue;
        }

        result << part;

        partIds.insert(part->id());
    }

    return result;
}

InstrumentList NotationParts::instrumentList(const QString& partId) const
{
    Part* part = this->part(partId);
    if (!part) {
        LOGW() << "Part not found" << partId;
        return InstrumentList();
    }

    InstrumentList result;

    auto instrumentList = part->instruments();
    for (auto it = instrumentList->begin(); it != instrumentList->end(); it++) {
        result << it->second;
    }

    return result;
}

StaffList NotationParts::staffList(const QString& partId, const QString& instrumentId) const
{
    Part* part = this->part(partId);
    if (!part) {
        LOGW() << "Part not found" << partId;
        return StaffList();
    }

    StaffList result;

    auto instrumentList = part->instruments();
    int staffGlobalIndex = 0;
    for (auto it = instrumentList->begin(); it != instrumentList->end(); it++) {
        if (it->second->instrumentId() == instrumentId) {
            for (int staffLocalIndex = 0; staffLocalIndex < it->second->nstaves(); staffLocalIndex++) {
                result << part->staff(staffGlobalIndex + staffLocalIndex);
            }
            return result;
        }
        staffGlobalIndex += it->second->nstaves();
    }
    return result;
}

void NotationParts::setPartVisible(const QString& partId, bool visible)
{
    Part* part = this->part(partId);
    if (!part) {
        part = this->part(partId, masterScore());
        if (!part) {
            LOGW() << "Part not found" << partId;
            return;
        }

        appendPart(part);
        m_partsChanged.notify();
        return;
    }

    masterScore()->startCmd();
    part->undoChangeProperty(Ms::Pid::VISIBLE, visible);
    masterScore()->endCmd();

    m_partChanged.send(part);
    m_partsChanged.notify();
}

void NotationParts::setInstrumentVisible(const QString& partId, const QString& instrumentId, bool visible)
{
    Part* part = this->part(partId);
    if (!part) {
        LOGW() << "Part not found" << partId;
        return;
    }

    masterScore()->startCmd();

    auto instrumentList = part->instruments();
    int staffGlobalIndex = 0;
    for (auto it = instrumentList->begin(); it != instrumentList->end(); it++) {
        if (it->second->instrumentId() == instrumentId) {
            for (int staffLocalIndex = 0; staffLocalIndex < it->second->nstaves(); staffLocalIndex++) {
                setStaffVisible(staffGlobalIndex + staffLocalIndex, visible);
            }

            masterScore()->endCmd();

            m_instrumentChanged.send(it->second);
            m_partsChanged.notify();
            return;
        }
        staffGlobalIndex += it->second->nstaves();
    }
}

void NotationParts::setStaffVisible(int staffIndex, bool visible)
{
    Staff* staff = this->staff(staffIndex);
    if (!staff) {
        return;
    }

    staff->setInvisible(!visible);

    score()->undo(new Ms::ChangeStaff(staff));
    masterScore()->endCmd();

    m_staffChanged.send(staff);
    m_partsChanged.notify();
}

void NotationParts::setStaffType(int staffIndex, StaffType type)
{
    Staff* staff = this->staff(staffIndex);
    const Ms::StaffType* staffType = Ms::StaffType::preset(type);

    if (!staff || !staffType) {
        return;
    }

    score()->undo(new Ms::ChangeStaffType(staff, *staffType));
    masterScore()->endCmd();

    m_staffChanged.send(staff);
    m_partsChanged.notify();
}

void NotationParts::setCutaway(int staffIndex, bool value)
{
    Staff* staff = this->staff(staffIndex);
    if (!staff) {
        return;
    }

    staff->setCutaway(value);

    score()->undo(new Ms::ChangeStaff(staff));
    masterScore()->endCmd();

    m_staffChanged.send(staff);
    m_partsChanged.notify();
}

void NotationParts::setSmallStaff(int staffIndex, bool value)
{
    Staff* staff = this->staff(staffIndex);
    Ms::StaffType* staffType = staff->staffType(Ms::Fraction(0, 1));

    if (!staff || !staffType) {
        return;
    }

    staffType->setSmall(value);

    score()->undo(new Ms::ChangeStaffType(staff, *staffType));
    masterScore()->endCmd();

    m_staffChanged.send(staff);
    m_partsChanged.notify();
}

void NotationParts::setVoiceVisible(int staffIndex, int voiceIndex, bool visible)
{
    Staff* staff = this->staff(staffIndex);
    if (!staff) {
        return;
    }

    staff->setPlaybackVoice(voiceIndex, visible);

    switch (voiceIndex) {
    case 0:
        staff->undoChangeProperty(Ms::Pid::PLAYBACK_VOICE1, visible);
        break;
    case 1:
        staff->undoChangeProperty(Ms::Pid::PLAYBACK_VOICE2, visible);
        break;
    case 2:
        staff->undoChangeProperty(Ms::Pid::PLAYBACK_VOICE3, visible);
        break;
    case 3:
        staff->undoChangeProperty(Ms::Pid::PLAYBACK_VOICE4, visible);
        break;
    }

    masterScore()->endCmd();

    m_staffChanged.send(staff);
    m_partsChanged.notify();
}

Staff* NotationParts::appendStaff(const QString& partId, const QString& instrumentId)
{
    Part* part = this->part(partId);
    if (!part) {
        LOGW() << "Part not found" << partId;
        return nullptr;
    }

    masterScore()->startCmd();

    auto instrumentList = part->instruments();
    int staffGlobalIndex = 0;
    for (auto it = instrumentList->begin(); it != instrumentList->end(); it++) {
        if (it->second->instrumentId() == instrumentId) {
            int lastStaffLocalIndex = it->second->nstaves() - 1;
            int lastStaffGlobalIndex = part->staves()->at(staffGlobalIndex + lastStaffLocalIndex)->idx();

            Staff* staff = part->staves()->at(staffGlobalIndex + lastStaffLocalIndex)->clone();
            score()->undoInsertStaff(staff, lastStaffGlobalIndex + 1);

            it->second->setClefType(staffGlobalIndex + it->second->nstaves(), staff->defaultClefType());

            masterScore()->endCmd();

            m_instrumentChanged.send(it->second);
            m_partsChanged.notify();

            return staff;
        }
        staffGlobalIndex += it->second->nstaves();
    }

    return nullptr;
}

Staff* NotationParts::appendLinkedStaff(int staffIndex)
{
    Staff* staff = this->staff(staffIndex);
    if (!staff) {
        return nullptr;
    }

    Part* part = staff->part();
    if (!part) {
        return nullptr;
    }

    Staff* linkedStaff = new Staff(score());

    linkedStaff->setPart(part);
    linkedStaff->linkTo(staff);

    int linkedStaffIndex = part->staves()->last()->idx();

    score()->undoInsertStaff(linkedStaff, linkedStaffIndex);
    masterScore()->endCmd();

    Instrument* instrument = this->instrument(linkedStaff);
    m_instrumentChanged.send(instrument);
    m_partsChanged.notify();

    return linkedStaff;
}

Channel<Part*> NotationParts::partChanged() const
{
    return m_partChanged;
}

Channel<Instrument*> NotationParts::instrumentChanged() const
{
    return m_instrumentChanged;
}

Channel<Staff*> NotationParts::staffChanged() const
{
    return m_staffChanged;
}

void NotationParts::removeParts(const std::vector<QString>& partsIds)
{
    if (partsIds.empty()) {
        return;
    }

    masterScore()->startCmd();

    for (const QString& partId: partsIds) {
        Part* part = this->part(partId);
        int firstStaffIndex = score()->staffIdx(part);

        score()->undoRemovePart(part, firstStaffIndex);
        m_partChanged.send(part);
    }

    masterScore()->endCmd();

    m_partsChanged.notify();
}

void NotationParts::removeStaves(const std::vector<int>& stavesIndexes)
{
    if (stavesIndexes.empty()) {
        return;
    }

    masterScore()->startCmd();

    for (int staffIndex: stavesIndexes) {
        Staff* staff = this->staff(staffIndex);
        Instrument* instrument = this->instrument(staff);

        score()->cmdRemoveStaff(staffIndex);
        m_instrumentChanged.send(instrument);
    }

    masterScore()->endCmd();

    m_partsChanged.notify();
}

void NotationParts::moveStaff(int fromIndex, int toIndex)
{
    Staff* staff = this->staff(fromIndex);
    if (!staff) {
        return;
    }

    Instrument* fromInstrument = this->instrument(staff);

    score()->undoRemoveStaff(staff);
    score()->undoInsertStaff(staff, toIndex);
    masterScore()->endCmd();

    Instrument* toInstrument = this->instrument(staff);

    m_instrumentChanged.send(fromInstrument);
    m_instrumentChanged.send(toInstrument);
    m_partsChanged.notify();
}

Notification NotationParts::partsChanged() const
{
    return m_partsChanged;
}

PartList NotationParts::scoreParts(const Ms::Score* score) const
{
    PartList result;

    for (Part* part: score->parts()) {
        result << part;
    }

    return result;
}

PartList NotationParts::excerptParts(const Ms::Score* score) const
{
    if (!score->isMaster()) {
        return PartList();
    }

    PartList result;

    for (const Ms::Excerpt* excerpt: score->excerpts()) {
        for (Part* part: excerpt->parts()) {
            result << part;
        }
    }

    return result;
}

Part* NotationParts::part(const QString& partId, const Ms::Score* score) const
{
    if (!score) {
        score = this->score();
    }

    PartList _parts;
    _parts << scoreParts(score) << excerptParts(score);
    for (Part* part: _parts) {
        if (part->id() == partId) {
            return part;
        }
    }

    return nullptr;
}

Instrument* NotationParts::instrument(const QString& partId, const QString& instrumentId) const
{
    Part* part = this->part(partId);
    if (!part) {
        return nullptr;
    }

    auto instrumentList = part->instruments();
    for (auto it = instrumentList->begin(); it != instrumentList->end(); it++) {
        if (it->second->instrumentId() == instrumentId) {
            return it->second;
        }
    }

    return nullptr;
}

Instrument* NotationParts::instrument(const Staff* staff) const
{
    if (!staff) {
        return nullptr;
    }

    Part* part = staff->part();
    if (!part) {
        return nullptr;
    }

    auto instrumentList = part->instruments();
    int staffGlobalIndex = 0;
    for (auto it = instrumentList->begin(); it != instrumentList->end(); it++) {
        for (int staffLocalIndex = 0; staffLocalIndex < it->second->nstaves(); staffLocalIndex++) {
            if (part->staff(staffGlobalIndex + staffLocalIndex)->idx() == staff->idx()) {
                return it->second;
            }
        }
        staffGlobalIndex += it->second->nstaves();
    }

    return nullptr;
}

Staff* NotationParts::staff(int staffIndex) const
{
    Staff* staff = score()->staff(staffIndex);

    if (!staff) {
        LOGW() << "Could not find staff with index:" << staffIndex;
    }

    return staff;
}

void NotationParts::appendPart(Part* part)
{
    for (Staff* partStaff: *part->staves()) {
        Staff* staff = new Staff(score());
        staff->setPart(part);
        staff->init(partStaff);
        if (partStaff->links() && !part->staves()->isEmpty()) {
            Staff* linkedStaff = part->staves()->back();
            staff->linkTo(linkedStaff);
        }
        part->insertStaff(staff, -1);
        score()->staves().append(staff);
    }

    score()->appendPart(part);
}
