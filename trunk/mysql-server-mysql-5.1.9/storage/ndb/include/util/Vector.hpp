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

#ifndef NDB_VECTOR_HPP
#define NDB_VECTOR_HPP

#include <ndb_global.h>
#include <NdbMutex.h>

template<class T>
struct Vector {
public:
  Vector(int sz = 10);
  ~Vector();

  T& operator[](unsigned i);
  const T& operator[](unsigned i) const;
  unsigned size() const { return m_size; };
  
  void push_back(const T &);
  void push(const T&, unsigned pos);
  T& set(T&, unsigned pos, T& fill_obj);
  T& back();
  
  void erase(unsigned index);
  
  void clear();
  
  void fill(unsigned new_size, T & obj);

  Vector<T>& operator=(const Vector<T>&);

  T* getBase() { return m_items;}
  const T* getBase() const { return m_items;}
private:
  T * m_items;
  unsigned m_size;
  unsigned m_incSize;
  unsigned m_arraySize;
};

template<class T>
Vector<T>::Vector(int i){
  m_items = new T[i];
  m_size = 0;
  m_arraySize = i;
  m_incSize = 50;
}

template<class T>
Vector<T>::~Vector(){
  delete[] m_items;
  // safety for placement new usage
  m_items = 0;
  m_size = 0;
  m_arraySize = 0;
}

template<class T>
T &
Vector<T>::operator[](unsigned i){
  if(i >= m_size)
    abort();
  return m_items[i];
}

template<class T>
const T &
Vector<T>::operator[](unsigned i) const {
  if(i >= m_size)
    abort();
  return m_items[i];
}

template<class T>
T &
Vector<T>::back(){
  return (* this)[m_size - 1];
}

template<class T>
void
Vector<T>::push_back(const T & t){
  if(m_size == m_arraySize){
    T * tmp = new T [m_arraySize + m_incSize];
    for (unsigned k = 0; k < m_size; k++)
      tmp[k] = m_items[k];
    delete[] m_items;
    m_items = tmp;
    m_arraySize = m_arraySize + m_incSize;
  }
  m_items[m_size] = t;
  m_size++;
}

template<class T>
void
Vector<T>::push(const T & t, unsigned pos)
{
  push_back(t);
  if (pos < m_size - 1)
  {
    for(unsigned i = m_size - 1; i > pos; i--)
    {
      m_items[i] = m_items[i-1];
    }
    m_items[pos] = t;
  }
}

template<class T>
T&
Vector<T>::set(T & t, unsigned pos, T& fill_obj)
{
  fill(pos, fill_obj);
  T& ret = m_items[pos];
  m_items[pos] = t;
  return ret;
}

template<class T>
void
Vector<T>::erase(unsigned i){
  if(i >= m_size)
    abort();
  
  for (unsigned k = i; k + 1 < m_size; k++)
    m_items[k] = m_items[k + 1];
  m_size--;
}

template<class T>
void
Vector<T>::clear(){
  m_size = 0;
}

template<class T>
void 
Vector<T>::fill(unsigned new_size, T & obj){
  while(m_size <= new_size)
    push_back(obj);
}

template<class T>
Vector<T>& 
Vector<T>::operator=(const Vector<T>& obj){
  if(this != &obj){
    clear();
    for(size_t i = 0; i<obj.size(); i++){
      push_back(obj[i]);
    }
  }
  return * this;
}

template<class T>
struct MutexVector : public NdbLockable {
  MutexVector(int sz = 10);
  ~MutexVector();

  T& operator[](unsigned i);
  const T& operator[](unsigned i) const;
  unsigned size() const { return m_size; };
  
  void push_back(const T &);
  void push_back(const T &, bool lockMutex);
  T& back();
  
  void erase(unsigned index);
  void erase(unsigned index, bool lockMutex);

  void clear();
  void clear(bool lockMutex);

  void fill(unsigned new_size, T & obj);
private:
  T * m_items;
  unsigned m_size;
  unsigned m_incSize;
  unsigned m_arraySize;
};

template<class T>
MutexVector<T>::MutexVector(int i){
  m_items = new T[i];
  m_size = 0;
  m_arraySize = i;
  m_incSize = 50;
}

template<class T>
MutexVector<T>::~MutexVector(){
  delete[] m_items;
  // safety for placement new usage
  m_items = 0;
  m_size = 0;
  m_arraySize = 0;
}

template<class T>
T &
MutexVector<T>::operator[](unsigned i){
  if(i >= m_size)
    abort();
  return m_items[i];
}

template<class T>
const T &
MutexVector<T>::operator[](unsigned i) const {
  if(i >= m_size)
    abort();
  return m_items[i];
}

template<class T>
T &
MutexVector<T>::back(){
  return (* this)[m_size - 1];
}

template<class T>
void
MutexVector<T>::push_back(const T & t){
  lock();
  if(m_size == m_arraySize){
    T * tmp = new T [m_arraySize + m_incSize];
    for (unsigned k = 0; k < m_size; k++)
      tmp[k] = m_items[k];
    delete[] m_items;
    m_items = tmp;
    m_arraySize = m_arraySize + m_incSize;
  }
  m_items[m_size] = t;
  m_size++;
  unlock();
}

template<class T>
void
MutexVector<T>::push_back(const T & t, bool lockMutex){
  if(lockMutex) 
    lock();
  if(m_size == m_arraySize){
    T * tmp = new T [m_arraySize + m_incSize];
    for (unsigned k = 0; k < m_size; k++)
      tmp[k] = m_items[k];
    delete[] m_items;
    m_items = tmp;
    m_arraySize = m_arraySize + m_incSize;
  }
  m_items[m_size] = t;
  m_size++;
  if(lockMutex)
    unlock();
}

template<class T>
void
MutexVector<T>::erase(unsigned i){
  if(i >= m_size)
    abort();
  
  lock();
  for (unsigned k = i; k + 1 < m_size; k++)
    m_items[k] = m_items[k + 1];
  m_size--;
  unlock();
}

template<class T>
void
MutexVector<T>::erase(unsigned i, bool _lock){
  if(i >= m_size)
    abort();
  
  if(_lock) 
    lock();
  for (unsigned k = i; k + 1 < m_size; k++)
    m_items[k] = m_items[k + 1];
  m_size--;
  if(_lock) 
    unlock();
}

template<class T>
void
MutexVector<T>::clear(){
  lock();
  m_size = 0;
  unlock();
}

template<class T>
void
MutexVector<T>::clear(bool l){
  if(l) lock();
  m_size = 0;
  if(l) unlock();
}

template<class T>
void 
MutexVector<T>::fill(unsigned new_size, T & obj){
  while(m_size <= new_size)
    push_back(obj);
}

#endif
