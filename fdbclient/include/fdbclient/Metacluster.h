/*
 * Metacluster.h
 *
 * This source file is part of the FoundationDB open source project
 *
 * Copyright 2013-2022 Apple Inc. and the FoundationDB project authors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef FDBCLIENT_METACLUSTER_H
#define FDBCLIENT_METACLUSTER_H
#include "fdbclient/CoordinationInterface.h"
#include "json_spirit/json_spirit_value.h"
#pragma once

#include "fdbclient/FDBTypes.h"
#include "fdbclient/KeyBackedTypes.h"
#include "flow/flat_buffers.h"

struct ClusterUsage {
	int numTenantGroups = 0;

	ClusterUsage() = default;
	ClusterUsage(int numTenantGroups) : numTenantGroups(numTenantGroups) {}

	json_spirit::mObject toJson() const;

	bool operator==(const ClusterUsage& other) const noexcept { return numTenantGroups == other.numTenantGroups; }
	bool operator!=(const ClusterUsage& other) const noexcept { return !(*this == other); }
	bool operator<(const ClusterUsage& other) const noexcept { return numTenantGroups < other.numTenantGroups; }

	template <class Ar>
	void serialize(Ar& ar) {
		serializer(ar, numTenantGroups);
	}
};

template <>
struct Traceable<ClusterUsage> : std::true_type {
	static std::string toString(const ClusterUsage& value) {
		return format("NumTenantGroups: %d", value.numTenantGroups);
	}
};

std::string clusterTypeToString(const ClusterType& clusterType);

// Represents the various states that a data cluster could be in.
//
// READY - the data cluster is active
// REMOVING - the data cluster is being removed and cannot have its configuration changed or any tenants created
// RESTORING - the data cluster is being restored and cannot have its configuration changed or any tenants
//             created/updated/deleted.
enum class DataClusterState { READY, REMOVING, RESTORING };

struct DataClusterEntry {
	constexpr static FileIdentifier file_identifier = 929511;

	static std::string clusterStateToString(DataClusterState clusterState);
	static DataClusterState stringToClusterState(std::string stateStr);

	UID id;
	ClusterUsage capacity;
	ClusterUsage allocated;

	DataClusterState clusterState = DataClusterState::READY;

	DataClusterEntry() = default;
	DataClusterEntry(ClusterUsage capacity) : capacity(capacity) {}
	DataClusterEntry(UID id, ClusterUsage capacity, ClusterUsage allocated)
	  : id(id), capacity(capacity), allocated(allocated) {}

	// Returns true if all configurable properties match
	bool matchesConfiguration(DataClusterEntry const& other) const {
		return id == other.id && capacity == other.capacity;
	}

	bool hasCapacity() const { return allocated < capacity; }

	Value encode() const { return ObjectWriter::toValue(*this, IncludeVersion()); }
	static DataClusterEntry decode(ValueRef const& value) {
		return ObjectReader::fromStringRef<DataClusterEntry>(value, IncludeVersion());
	}

	json_spirit::mObject toJson() const;

	template <class Ar>
	void serialize(Ar& ar) {
		serializer(ar, id, capacity, allocated, clusterState);
	}
};

struct MetaclusterMetrics {
	int numTenants = 0;
	int numDataClusters = 0;
	int tenantGroupCapacity = 0;
	int tenantGroupsAllocated = 0;

	MetaclusterMetrics() = default;
};

struct MetaclusterRegistrationEntry {
	constexpr static FileIdentifier file_identifier = 13448589;

	ClusterType clusterType;

	ClusterName metaclusterName;
	ClusterName name;
	UID metaclusterId;
	UID id;

	MetaclusterRegistrationEntry() = default;
	MetaclusterRegistrationEntry(ClusterName metaclusterName, UID metaclusterId)
	  : clusterType(ClusterType::METACLUSTER_MANAGEMENT), metaclusterName(metaclusterName), name(metaclusterName),
	    metaclusterId(metaclusterId), id(metaclusterId) {}
	MetaclusterRegistrationEntry(ClusterName metaclusterName, ClusterName name, UID metaclusterId, UID id)
	  : clusterType(ClusterType::METACLUSTER_DATA), metaclusterName(metaclusterName), name(name),
	    metaclusterId(metaclusterId), id(id) {
		ASSERT(metaclusterName != name && metaclusterId != id);
	}

	// Returns true if this entry is associated with the same cluster as the passed in entry. If one entry is from the
	// management cluster and the other is from a data cluster, this checks whether they are part of the same
	// metacluster.
	bool matches(MetaclusterRegistrationEntry const& other) const {
		if (metaclusterName != other.metaclusterName || metaclusterId != other.metaclusterId) {
			return false;
		} else if (clusterType == ClusterType::METACLUSTER_DATA && other.clusterType == ClusterType::METACLUSTER_DATA &&
		           (name != other.name || id != other.id)) {
			return false;
		}

		return true;
	}

	MetaclusterRegistrationEntry toManagementClusterRegistration() const {
		ASSERT(clusterType == ClusterType::METACLUSTER_DATA);
		return MetaclusterRegistrationEntry(metaclusterName, metaclusterId);
	}

	MetaclusterRegistrationEntry toDataClusterRegistration(ClusterName name, UID id) const {
		ASSERT(clusterType == ClusterType::METACLUSTER_MANAGEMENT);
		return MetaclusterRegistrationEntry(metaclusterName, name, metaclusterId, id);
	}

	Value encode() const { return ObjectWriter::toValue(*this, IncludeVersion()); }
	static MetaclusterRegistrationEntry decode(ValueRef const& value) {
		return ObjectReader::fromStringRef<MetaclusterRegistrationEntry>(value, IncludeVersion());
	}
	static Optional<MetaclusterRegistrationEntry> decode(Optional<Value> value) {
		return value.map<MetaclusterRegistrationEntry>(
		    [](ValueRef const& v) { return MetaclusterRegistrationEntry::decode(v); });
	}

	std::string toString() const {
		if (clusterType == ClusterType::METACLUSTER_MANAGEMENT) {
			return fmt::format(
			    "metacluster name: {}, metacluster id: {}", printable(metaclusterName), metaclusterId.shortString());
		} else {
			return fmt::format("metacluster name: {}, metacluster id: {}, data cluster name: {}, data cluster id: {}",
			                   printable(metaclusterName),
			                   metaclusterId.shortString(),
			                   printable(name),
			                   id.shortString());
		}
	}

	template <class Ar>
	void serialize(Ar& ar) {
		serializer(ar, clusterType, metaclusterName, name, metaclusterId, id);
	}
};

template <>
struct Traceable<MetaclusterRegistrationEntry> : std::true_type {
	static std::string toString(MetaclusterRegistrationEntry const& entry) { return entry.toString(); }
};

struct MetaclusterMetadata {
	// Registration information for a metacluster, stored on both management and data clusters
	static KeyBackedObjectProperty<MetaclusterRegistrationEntry, decltype(IncludeVersion())>& metaclusterRegistration();
};

#endif
