#pragma once

#include <protocols/mbus/client.hpp>
#include <string>
#include <unordered_map>

#include "device.hpp"
#include "sysfs.hpp"

namespace drvcore {

struct UeventProperties {
	auto begin() { return map.begin(); }

	auto end() { return map.end(); }

	void set(std::string name, std::string value) { map[name] = value; }

  private:
	std::unordered_map<std::string, std::string> map;
};

struct ClassDevice;

// This struct corresponds to Linux' struct Device (i.e. a device that is part of sysfs).
// TODO: Make the sysfs::Object private?
struct Device : sysfs::Object {
	Device(std::shared_ptr<Device> parent, std::string name, UnixDevice *unix_device);

  protected:
	~Device() = default;

  public:
	void setupDevicePtr(std::weak_ptr<Device> self) { _devicePtr = std::move(self); }

	std::shared_ptr<Device> devicePtr() { return std::shared_ptr<Device>(_devicePtr); }

	std::shared_ptr<Device> parentDevice() { return _parentDevice; }

	UnixDevice *unixDevice() { return _unixDevice; }

	std::unordered_map<std::string, std::shared_ptr<ClassDevice>> &classDevices() {
		return _classDevices;
	}

	// Returns the path of this device under /sys/devices.
	std::string getSysfsPath();

	void composeStandardUevent(UeventProperties &);

	virtual void linkToSubsystem();

	virtual void composeUevent(UeventProperties &) = 0;

  private:
	std::weak_ptr<Device> _devicePtr;
	UnixDevice *_unixDevice;
	std::shared_ptr<Device> _parentDevice;

	std::unordered_map<std::string, std::shared_ptr<ClassDevice>> _classDevices;
};

struct BusSubsystem {
	BusSubsystem(std::string name);

	std::shared_ptr<sysfs::Object> object() { return _object; }
	std::shared_ptr<sysfs::Object> devicesObject() { return _devicesObject; }
	std::shared_ptr<sysfs::Object> driversObject() { return _driversObject; }

  private:
	std::shared_ptr<sysfs::Object> _object;
	std::shared_ptr<sysfs::Object> _devicesObject;
	std::shared_ptr<sysfs::Object> _driversObject;
};

struct BusDevice : Device {
	BusDevice(
	    BusSubsystem *subsystem,
	    std::string name,
	    UnixDevice *unix_device,
	    std::shared_ptr<Device> parent = nullptr
	);

  protected:
	~BusDevice() = default;

  public:
	void linkToSubsystem() override;

  private:
	BusSubsystem *_subsystem;
};

struct BusDriver : sysfs::Object {
	BusDriver(std::shared_ptr<BusSubsystem> parent, std::string name)
	    : sysfs::Object(parent->driversObject(), name) {}
};

struct ClassSubsystem {
	ClassSubsystem(std::string name);

	std::shared_ptr<sysfs::Object> object() { return _object; }

  private:
	std::shared_ptr<sysfs::Object> _object;
};

struct ClassDevice : Device {
	ClassDevice(
	    ClassSubsystem *subsystem,
	    std::shared_ptr<Device> parent,
	    std::string name,
	    UnixDevice *unix_device
	);

  protected:
	~ClassDevice() = default;

  public:
	void linkToSubsystem() override;

  private:
	ClassSubsystem *_subsystem;
};

struct BlockDevice : Device {
	BlockDevice(
	    ClassSubsystem *subsystem,
	    std::shared_ptr<Device> parent,
	    std::string name,
	    UnixDevice *unix_device
	);

  protected:
	~BlockDevice() = default;

  public:
	void linkToSubsystem() override;

  private:
	ClassSubsystem *_subsystem;
};

void initialize();

void registerMbusDevice(mbus_ng::EntityId, std::shared_ptr<Device>);
std::shared_ptr<Device> getMbusDevice(mbus_ng::EntityId);

void installDevice(std::shared_ptr<Device> device);

namespace udev {

void emitAddEvent(std::string devpath, UeventProperties &ue);
void emitChangeEvent(std::string devpath, UeventProperties &ue);

} // namespace udev

} // namespace drvcore
