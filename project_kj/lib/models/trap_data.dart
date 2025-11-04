import 'package:cloud_firestore/cloud_firestore.dart';

class TrapData {
  const TrapData({
    required this.ratDetectedCount,
    required this.cyclesCompleted,
    required this.activateTrap,
    required this.openFeedDispenser,
    required this.co2Release,
    required this.cyclesNeededForCo2Release,
    this.lastUpdated,
  });

  final int ratDetectedCount;
  final int cyclesCompleted;
  final bool activateTrap;
  final bool openFeedDispenser;
  final bool co2Release;
  final int cyclesNeededForCo2Release;
  final DateTime? lastUpdated;

  TrapData copyWith({
    int? ratDetectedCount,
    int? cyclesCompleted,
    bool? activateTrap,
    bool? openFeedDispenser,
    bool? co2Release,
    int? cyclesNeededForCo2Release,
    DateTime? lastUpdated,
  }) {
    return TrapData(
      ratDetectedCount: ratDetectedCount ?? this.ratDetectedCount,
      cyclesCompleted: cyclesCompleted ?? this.cyclesCompleted,
      activateTrap: activateTrap ?? this.activateTrap,
      openFeedDispenser: openFeedDispenser ?? this.openFeedDispenser,
      co2Release: co2Release ?? this.co2Release,
      cyclesNeededForCo2Release:
          cyclesNeededForCo2Release ?? this.cyclesNeededForCo2Release,
      lastUpdated: lastUpdated ?? this.lastUpdated,
    );
  }

  Map<String, dynamic> toMap() {
    return <String, dynamic>{
      'rat_detected_count': ratDetectedCount,
      'cycle_completed': cyclesCompleted,
      'activate_trap': activateTrap,
      'open_feed_dispenser': openFeedDispenser,
      'co2_release': co2Release,
      'cycles_needed_for_co2_release': cyclesNeededForCo2Release,
      'updated_at': lastUpdated == null
          ? FieldValue.serverTimestamp()
          : Timestamp.fromDate(lastUpdated!),
    };
  }

  factory TrapData.fromSnapshot(
    DocumentSnapshot<Map<String, dynamic>> snapshot,
  ) {
    final data = snapshot.data() ?? <String, dynamic>{};

    DateTime? updatedAt;
    final Object? rawTimestamp = data['updated_at'];
    if (rawTimestamp is Timestamp) {
      updatedAt = rawTimestamp.toDate();
    } else if (rawTimestamp is DateTime) {
      updatedAt = rawTimestamp;
    }

    return TrapData(
      ratDetectedCount: _intFrom(data['rat_detected_count']),
      cyclesCompleted: _intFrom(data['cycle_completed']),
      activateTrap: _boolFrom(data['activate_trap']),
      openFeedDispenser: _boolFrom(data['open_feed_dispenser']),
      co2Release: _boolFrom(data['co2_release']),
      cyclesNeededForCo2Release:
          _intFrom(data['cycles_needed_for_co2_release'], fallback: 1),
      lastUpdated: updatedAt,
    );
  }

  static int _intFrom(Object? value, {int fallback = 0}) {
    if (value is int) {
      return value;
    }
    if (value is double) {
      return value.toInt();
    }
    if (value is String) {
      return int.tryParse(value) ?? fallback;
    }
    return fallback;
  }

  static bool _boolFrom(Object? value, {bool fallback = false}) {
    if (value is bool) {
      return value;
    }
    if (value is num) {
      return value != 0;
    }
    if (value is String) {
      return value.toLowerCase() == 'true';
    }
    return fallback;
  }

  static TrapData defaults() {
    return const TrapData(
      ratDetectedCount: 0,
      cyclesCompleted: 0,
      activateTrap: false,
      openFeedDispenser: false,
      co2Release: false,
      cyclesNeededForCo2Release: 1,
    );
  }
}
