import 'package:cloud_firestore/cloud_firestore.dart';

class TrapLogEntry {
  TrapLogEntry({
    required this.id,
    required this.label,
    required this.payload,
    required this.timestamp,
  });

  final String id;
  final String label;
  final Map<String, dynamic> payload;
  final DateTime timestamp;

  factory TrapLogEntry.fromSnapshot(
    QueryDocumentSnapshot<Map<String, dynamic>> doc,
  ) {
    final data = doc.data();
    final timestamp = data['timestamp'];
    DateTime parsedTimestamp;
    if (timestamp is Timestamp) {
      parsedTimestamp = timestamp.toDate();
    } else if (timestamp is DateTime) {
      parsedTimestamp = timestamp;
    } else {
      parsedTimestamp = DateTime.now();
    }

    return TrapLogEntry(
      id: doc.id,
      label: data['label'] as String? ?? 'Action',
      payload: (data['payload'] as Map<String, dynamic>?) ?? <String, dynamic>{},
      timestamp: parsedTimestamp,
    );
  }
}
