#pragma once
#include <MatterEndpoints/MatterTemperatureControlledCabinet.h>
#include <string.h>

/**
 * A generic Matter numeric control endpoint that maps to 
 * TemperatureSetpoint in the TemperatureControl cluster.
 */
template <typename T>
class MatterNumericEndpoint : public MatterTemperatureControlledCabinet {
public:
  using Callback = std::function<void(T)>;
  
  /**
   * Set a callback to be invoked when the attribute changes.
   * For double, it returns the raw value / 100.0.
   * For integral types, it returns the floor of value / 100.
   */
  void onChange(Callback cb) { _onChangeCb = cb; }

  bool attributeChangeCB(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *val) override {
    bool ret = MatterTemperatureControlledCabinet::attributeChangeCB(endpoint_id, cluster_id, attribute_id, val);
    
    if (endpoint_id == getEndPointId() &&
        cluster_id == chip::app::Clusters::TemperatureControl::Id &&
        attribute_id == chip::app::Clusters::TemperatureControl::Attributes::TemperatureSetpoint::Id &&
        _onChangeCb) {
      
      if (std::is_floating_point<T>::value) {
        _onChangeCb(static_cast<T>(val->val.i16 / 100.0));
      } else {
        _onChangeCb(static_cast<T>(val->val.i16 / 100));
      }
    }
    return ret;
  }

private:
  Callback _onChangeCb = nullptr;
};

/**
 * Utility to set device identification strings on the Root Node (Endpoint 0).
 */
inline void setDeviceIdentification(const char* vendor, const char* product) {
  using namespace chip::app::Clusters;
  
  esp_matter_attr_val_t vName = esp_matter_char_str((char*)vendor, strlen(vendor));
  esp_matter::attribute::update(0, BasicInformation::Id, BasicInformation::Attributes::VendorName::Id, &vName);
  
  esp_matter_attr_val_t pName = esp_matter_char_str((char*)product, strlen(product));
  esp_matter::attribute::update(0, BasicInformation::Id, BasicInformation::Attributes::ProductName::Id, &pName);
}
