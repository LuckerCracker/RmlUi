/*
 * This source file is part of RmlUi, the HTML/CSS Interface Middleware
 *
 * For the latest information, see http://github.com/mikke89/RmlUi
 *
 * Copyright (c) 2008-2010 CodePoint Ltd, Shift Technology Ltd
 * Copyright (c) 2019-2023 The RmlUi Team, and contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */

#include "DamageTracker.h"

namespace Rml {

struct DamageContextState {
	bool full_damage = false;
	bool has_damage = false;
	Rectanglei union_rect = Rectanglei::MakeInvalid();
	UnorderedMap<Element*, Rectanglei> last_rendered_bbox;
};

static UnorderedMap<Context*, DamageContextState> damage_states;

static DamageContextState* GetDamageState(Context* context, bool create_if_missing)
{
	if (!context)
		return nullptr;

	auto it = damage_states.find(context);
	if (it != damage_states.end())
		return &it->second;

	if (!create_if_missing)
		return nullptr;

	auto insert_result = damage_states.emplace(context, DamageContextState{});
	return &insert_result.first->second;
}

void DamageTracker::MarkFull(Context* context)
{
	DamageContextState* state = GetDamageState(context, true);
	if (!state)
		return;

	state->full_damage = true;
	state->has_damage = true;
	state->union_rect = Rectanglei::MakeInvalid();
}

void DamageTracker::MarkRect(Context* context, Rectanglei rect)
{
	if (!rect.Valid())
		return;

	DamageContextState* state = GetDamageState(context, true);
	if (!state)
		return;

	state->has_damage = true;

	if (state->full_damage)
		return;

	if (state->union_rect.Valid())
		state->union_rect = state->union_rect.Join(rect);
	else
		state->union_rect = rect;
}

void DamageTracker::MarkOldBBox(Context* context, Element* element)
{
	DamageContextState* state = GetDamageState(context, false);
	if (!state || !element)
		return;

	auto it = state->last_rendered_bbox.find(element);
	if (it != state->last_rendered_bbox.end())
		MarkRect(context, it->second);
}

void DamageTracker::UpdateRenderedBBox(Context* context, Element* element, Rectanglei new_bbox)
{
	DamageContextState* state = GetDamageState(context, true);
	if (!state || !element)
		return;

	auto it = state->last_rendered_bbox.find(element);
	if (it != state->last_rendered_bbox.end())
	{
		const Rectanglei old_bbox = it->second;
		if (old_bbox != new_bbox)
			MarkRect(context, old_bbox.Join(new_bbox));

		it->second = new_bbox;
	}
	else
	{
		state->last_rendered_bbox.emplace(element, new_bbox);
	}
}

void DamageTracker::Unregister(Context* context, Element* element)
{
	DamageContextState* state = GetDamageState(context, false);
	if (!state || !element)
		return;

	state->last_rendered_bbox.erase(element);
}

DamageInfo DamageTracker::PeekDamage(Context* context)
{
	DamageContextState* state = GetDamageState(context, false);
	if (!state)
		return {};

	DamageInfo info;
	info.has_damage = state->has_damage;
	info.full_damage = state->full_damage;
	info.union_rect = state->union_rect;
	return info;
}

void DamageTracker::ClearDamage(Context* context)
{
	DamageContextState* state = GetDamageState(context, false);
	if (!state)
		return;

	state->has_damage = false;
	state->full_damage = false;
	state->union_rect = Rectanglei::MakeInvalid();
}

} // namespace Rml
