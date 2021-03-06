/* Copyright (C) 2003 MySQL AB

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA */


#include "Pool.hpp"
#include "SimulatedBlock.hpp"

void*
Pool_context::alloc_page(Uint32 type_id, Uint32 *i)
{
  return m_block->m_ctx.m_mm.alloc_page(type_id, i);
}
  
void 
Pool_context::release_page(Uint32 type_id, Uint32 i)
{
  m_block->m_ctx.m_mm.release_page(type_id, i);
}

void*
Pool_context::get_memroot()
{
  return m_block->m_ctx.m_mm.get_memroot();
}

void
Pool_context::handleAbort(int err, const char * msg)
{
  m_block->progError(__LINE__, err, msg);
}
